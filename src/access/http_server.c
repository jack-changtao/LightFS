#define _GNU_SOURCE
#include "http_server.h"
#include "http_response.h"
#include "access_routes.h"
#include "access_handlers.h"
#include "lightfs/access/sigv4.h"
#include "lightfs/access/s3_xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/uio.h>

#define DEFAULT_MAX_CONNECTIONS      1024
#define DEFAULT_KEEP_ALIVE_TIMEOUT   5000
#define DEFAULT_REQUEST_TIMEOUT      30000
#define DEFAULT_MAX_REQUEST_BODY     (64 * 1024 * 1024)
#define DEFAULT_RECV_BUF_SIZE        65536

/* ── helpers ─────────────────────────────────────────────────────── */

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void fill_error_response(s3_response_t *resp, uint32_t status, const char *code, const char *msg) {
  /* Safe under SPDK single-threaded reactor model */
  static char err_buf[512];
  memset(resp, 0, sizeof(*resp));
  resp->http_status = status;
  resp->content_type = "application/xml";
  int w = s3_xml_serialize_error(err_buf, sizeof(err_buf), code, msg);
  resp->body = err_buf;
  resp->body_length = (w > 0) ? (uint32_t)w : 0;
}

static void on_write_complete(void *cb_arg, int err) {
  http_conn_t *conn = (http_conn_t *)cb_arg;
  struct spdk_sock_request *req = conn->write_req;
  conn->write_req = NULL;
  http_conn_on_write_done(conn, err);
  free(req);
}

static void conn_write_response(http_conn_t *conn, const s3_response_t *resp) {
  struct iovec iov[HTTP_IOV_MAX];
  int iovcnt = http_response_serialize(resp, conn->parser_ctx.request.keep_alive, iov, HTTP_IOV_MAX);
  if (iovcnt < 0) {
    conn->state = CONN_CLOSING;
    return;
  }

  size_t req_size = sizeof(struct spdk_sock_request) + (size_t)iovcnt * sizeof(struct iovec);
  struct spdk_sock_request *req = malloc(req_size);
  if (!req) {
    conn->state = CONN_CLOSING;
    return;
  }
  req->cb_fn = on_write_complete;
  req->cb_arg = conn;
  req->iovcnt = iovcnt;
  memcpy(SPDK_SOCK_REQUEST_IOV(req, 0), iov, (size_t)iovcnt * sizeof(struct iovec));

  conn->write_req = req;
  conn->state = CONN_WRITING;
  spdk_sock_writev_async(conn->sock, req);
}

/* ── connection ops ──────────────────────────────────────────────── */

http_conn_t *http_conn_accept(http_server_t *server, struct spdk_sock *sock) {
  if (server->conn_count >= server->max_connections) {
    /* Over capacity: close immediately without allocating a connection */
    spdk_sock_close(&sock);
    return NULL;
  }

  http_conn_t *conn = calloc(1, sizeof(http_conn_t));
  if (!conn) return NULL;
  conn->sock = sock;
  conn->group = server->group;
  conn->server = server;
  conn->recv_buf_size = DEFAULT_RECV_BUF_SIZE;
  conn->recv_buf = malloc(conn->recv_buf_size);
  if (!conn->recv_buf) {
    free(conn);
    return NULL;
  }
  conn->state = CONN_READING;
  conn->last_active_ts = now_ms();
  http_parser_init(&conn->parser_ctx);
  conn->parser_ctx.max_body_size = server->max_request_body;

  spdk_sock_group_add_sock(server->group, sock, NULL, conn);

  TAILQ_INSERT_TAIL(&server->conn_list, conn, link);
  server->conn_count++;

  return conn;
}

void http_conn_on_readable(http_conn_t *conn) {
  conn->last_active_ts = now_ms();

  uint32_t space = conn->recv_buf_size - conn->recv_len;
  if (space < 4096) {
    conn->recv_buf_size *= 2;
    conn->recv_buf = realloc(conn->recv_buf, conn->recv_buf_size);
    if (!conn->recv_buf) {
      conn->state = CONN_CLOSING;
      return;
    }
    space = conn->recv_buf_size - conn->recv_len;
  }

  ssize_t nread = spdk_sock_recv(conn->sock,
                  conn->recv_buf + conn->recv_len, space);
  if (nread <= 0) {
    conn->state = CONN_CLOSING;
    return;
  }

  conn->recv_len += (uint32_t)nread;

  int result = http_parser_feed(&conn->parser_ctx,
                 conn->recv_buf, conn->recv_len);
  if (result < 0) {
    s3_response_t resp;
    fill_error_response(&resp, 400, "InvalidRequest", "HTTP parse error");
    conn->parser_ctx.request.keep_alive = false;
    conn_write_response(conn, &resp);
    return;
  }

  size_t consumed = (size_t)result;
  if (consumed < conn->recv_len) {
    memmove(conn->recv_buf, conn->recv_buf + consumed,
        conn->recv_len - consumed);
  }
  conn->recv_len -= (uint32_t)consumed;

  if (conn->parser_ctx.request.complete) {
    http_conn_dispatch(conn);
  }
}

void http_conn_dispatch(http_conn_t *conn) {
  conn->state = CONN_DISPATCHING;
  http_request_t *req = &conn->parser_ctx.request;

  int result = s3_route_parse(req->method, req->uri,
                conn->parser_ctx.header_ptrs,
                conn->parser_ctx.header_count * 2,
                &req->s3);
  if (result != 0) {
    s3_response_t resp;
    fill_error_response(&resp, 400, "InvalidRequest", "Could not parse request");
    conn_write_response(conn, &resp);
    return;
  }

  if (req->s3.authorization[0]) {
    sigv4_result_t auth = sigv4_validate(req->s3.authorization, req->method,
                       req->uri, req->s3.host,
                       req->s3.date, req->body,
                       req->body_len);
    if (auth != SIGV4_OK) {
      s3_response_t resp;
      fill_error_response(&resp, 403, "SignatureDoesNotMatch",
                "The request signature does not match");
      conn_write_response(conn, &resp);
      return;
    }
  }

  s3_response_t response = {0};
  switch (req->s3.operation) {
  case S3_OPERATION_PUT_OBJECT:
    result = s3_handler_put(&req->s3, req->body, req->body_len, &response);
    break;
  case S3_OPERATION_GET_OBJECT:
  case S3_OPERATION_HEAD_OBJECT:
    result = s3_handler_get(&req->s3, &response);
    /* HEAD returns headers only, no body */
    if (req->s3.operation == S3_OPERATION_HEAD_OBJECT) {
      response.body = NULL;
      response.body_length = 0;
    }
    break;
  case S3_OPERATION_DELETE_OBJECT:
    result = s3_handler_delete(&req->s3, &response);
    break;
  case S3_OPERATION_LIST_OBJECTS:
    result = s3_handler_list(&req->s3, &response);
    break;
  default:
    fill_error_response(&response, 405, "MethodNotAllowed", "");
    result = 0;
    break;
  }

  if (result != 0) {
    fill_error_response(&response, 500, "InternalError", "Handler failed");
  }

  conn_write_response(conn, &response);
}

void http_conn_on_write_done(http_conn_t *conn, int err) {
  (void)err;
  if (conn->parser_ctx.request.keep_alive && conn->state != CONN_CLOSING) {
    http_parser_reset(&conn->parser_ctx);
    conn->state = CONN_READING;
  } else {
    conn->state = CONN_CLOSING;
  }
}

void http_conn_close(http_conn_t *conn) {
  if (!conn) return;
  TAILQ_REMOVE(&conn->server->conn_list, conn, link);
  conn->server->conn_count--;
  spdk_sock_group_remove_sock(conn->group, conn->sock);
  spdk_sock_close(&conn->sock);
  http_request_destroy(&conn->parser_ctx.request);
  free(conn->write_req);
  free(conn->recv_buf);
  free(conn);
}

/* ── server lifecycle ────────────────────────────────────────────── */

http_server_t *http_server_create(const http_server_config_t *config) {
  http_server_t *server = calloc(1, sizeof(http_server_t));
  if (!server) return NULL;

  server->max_connections = config->max_connections > 0
                 ? config->max_connections
                 : DEFAULT_MAX_CONNECTIONS;
  server->keep_alive_timeout_ms = config->keep_alive_timeout_ms > 0
                    ? config->keep_alive_timeout_ms
                    : DEFAULT_KEEP_ALIVE_TIMEOUT;
  server->request_timeout_ms = config->request_timeout_ms > 0
                  ? config->request_timeout_ms
                  : DEFAULT_REQUEST_TIMEOUT;
  server->max_request_body = config->max_request_body > 0
                 ? config->max_request_body
                 : DEFAULT_MAX_REQUEST_BODY;
  TAILQ_INIT(&server->conn_list);

  struct spdk_sock_opts opts;
  spdk_sock_get_default_opts(&opts);
  opts.opts_size = sizeof(opts);

  server->listen_sock = spdk_sock_listen_ext(config->listen_host,
                        (int)config->listen_port,
                        NULL, &opts);
  if (!server->listen_sock) {
    free(server);
    return NULL;
  }

  server->group = spdk_sock_group_create(server);
  if (!server->group) {
    spdk_sock_close(&server->listen_sock);
    free(server);
    return NULL;
  }

  spdk_sock_group_add_sock(server->group, server->listen_sock, NULL, server);

  server->running = true;
  return server;
}

void http_server_destroy(http_server_t *server) {
  if (!server) return;
  server->running = false;

  http_conn_t *conn, *tmp;
  TAILQ_FOREACH_SAFE(conn, &server->conn_list, link, tmp) {
    http_conn_close(conn);
  }

  spdk_sock_group_remove_sock(server->group, server->listen_sock);
  spdk_sock_close(&server->listen_sock);
  spdk_sock_group_close(&server->group);
  free(server);
}

/* ── poller (called from SPDK reactor or manually in tests) ───────── */

static uint64_t last_timeout_check;

static void check_timeouts(http_server_t *server) {
  uint64_t now = now_ms();
  http_conn_t *conn, *tmp;

  TAILQ_FOREACH_SAFE(conn, &server->conn_list, link, tmp) {
    uint64_t idle = now - conn->last_active_ts;
    if (conn->state == CONN_READING && idle > server->request_timeout_ms) {
      http_conn_close(conn);
    } else if (conn->state == CONN_IDLE && idle > server->keep_alive_timeout_ms) {
      http_conn_close(conn);
    }
  }
}

void http_server_poll(http_server_t *server) {
  if (!server->running) return;

  int events = spdk_sock_group_poll(server->group);
  (void)events;

  struct spdk_sock *new_sock;
  while ((new_sock = spdk_sock_accept(server->listen_sock)) != NULL) {
    http_conn_accept(server, new_sock);
  }

  http_conn_t *conn, *tmp;
  TAILQ_FOREACH_SAFE(conn, &server->conn_list, link, tmp) {
    if (conn->state == CONN_READING || conn->state == CONN_IDLE) {
      http_conn_on_readable(conn);
    }
    if (conn->state == CONN_CLOSING) {
      http_conn_close(conn);
    }
  }

  uint64_t now = now_ms();
  if (now - last_timeout_check >= 1000) {
    check_timeouts(server);
    last_timeout_check = now;
  }
}

/* ── public wrapper (called from access_server) ──────────────────── */

int http_server_start(const http_server_config_t *config, struct http_server **out_server) {
  *out_server = http_server_create(config);
  return (*out_server) ? 0 : -1;
}

void http_server_stop(struct http_server *server) {
  http_server_destroy(server);
}
