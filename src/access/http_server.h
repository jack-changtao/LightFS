#ifndef LIGHTFS_ACCESS_HTTP_SERVER_H
#define LIGHTFS_ACCESS_HTTP_SERVER_H

#include "lightfs/access/http_server.h"
#include "http_parser.h"
#include <spdk/sock.h>
#include <spdk/queue.h>
#include <stdint.h>
#include <stdbool.h>

/* ── connection state ────────────────────────────────────────────── */

typedef enum {
    CONN_IDLE,        /* waiting for next request (keep-alive) */
    CONN_READING,     /* accumulating bytes, feeding llhttp */
    CONN_DISPATCHING, /* route → auth → handler */
    CONN_WRITING,     /* spdk_sock_writev_async pending */
    CONN_CLOSING,     /* write complete but not keep-alive, or error */
} conn_state_t;

/* ── connection ──────────────────────────────────────────────────── */

typedef struct http_conn {
    struct spdk_sock       *sock;
    http_parser_ctx_t      parser_ctx;
    uint8_t                *recv_buf;
    uint32_t               recv_buf_size;
    uint32_t               recv_len;
    conn_state_t           state;
    struct spdk_sock_group *group;
    struct http_server     *server;
    struct spdk_sock_request *write_req;
    TAILQ_ENTRY(http_conn) link;
    uint64_t               last_active_ts;
} http_conn_t;

/* ── server ──────────────────────────────────────────────────────── */

typedef struct http_server {
    struct spdk_sock    *listen_sock;
    struct spdk_sock_group *group;
    TAILQ_HEAD(, http_conn) conn_list;
    uint32_t            conn_count;
    uint32_t            max_connections;
    uint32_t            keep_alive_timeout_ms;
    uint32_t            request_timeout_ms;
    uint32_t            max_request_body;
    bool                running;
} http_server_t;

/* ── lifecycle ───────────────────────────────────────────────────── */

http_server_t *http_server_create(const http_server_config_t *config);
void http_server_destroy(http_server_t *server);

/* ── per-connection ops ──────────────────────────────────────────── */

void http_conn_on_readable(http_conn_t *conn);
void http_conn_on_write_done(http_conn_t *conn, int err);
http_conn_t *http_conn_accept(http_server_t *server, struct spdk_sock *sock);
void http_conn_dispatch(http_conn_t *conn);
void http_conn_close(http_conn_t *conn);

void http_server_poll(http_server_t *server);

#endif /* LIGHTFS_ACCESS_HTTP_SERVER_H */
