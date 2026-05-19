#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "mock_spdk_sock.h"
#include "http_server.h"

/*
* Integration test: round-trip from raw HTTP bytes through parser,
* dispatch, handler, and response serialization.
* Uses mock SPDK layer (no real TCP sockets).
* Verifies response via mock_written_iov.
*/

/* helper: set recv data using strlen (avoids off-by-one in manual counts) */
static void set_req(const char *req_str) {
  mock_sock_set_recv((const uint8_t *)req_str, strlen(req_str));
}

static void test_get_object_roundtrip(void) {
  mock_sock_reset();

  set_req("GET /mybucket/mykey.txt HTTP/1.1\r\n"
      "Host: s3.example.com\r\n"
      "\r\n");

  http_server_t *server = http_server_create(
    &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
  assert(server != NULL);

  http_server_poll(server);
  assert(server->conn_count == 1);

  /* verify mock received the write (response was sent) */
  assert(mock_written_iov != NULL);
  assert(mock_written_iovcnt > 0);

  /* first iovec: HTTP/1.1 */
  assert(memcmp(mock_written_iov[0].iov_base, "HTTP/1.1 ", 9) == 0);
  /* second iovec: status line */
  assert(memcmp(mock_written_iov[1].iov_base, "200 OK\r\n", 9) == 0);

  /* last iovec contains placeholder response body */
  assert(mock_written_iov[mock_written_iovcnt-1].iov_len > 0);

  http_server_destroy(server);
  printf("  PASS: get_object_roundtrip\n");
}

static void test_put_object_roundtrip(void) {
  mock_sock_reset();

  set_req("PUT /bucket/key HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "data");

  http_server_t *server = http_server_create(
    &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
  assert(server != NULL);

  http_server_poll(server);
  assert(server->conn_count == 1);

  /* verify response was written */
  assert(mock_written_iov != NULL);
  assert(mock_written_iovcnt > 0);
  assert(memcmp(mock_written_iov[1].iov_base, "200 OK\r\n", 9) == 0);

  /* body should contain the ETag */
  int found_etag = 0;
  for (int i = 0; i < mock_written_iovcnt; i++) {
    if (mock_written_iov[i].iov_len > 6 &&
      memcmp(mock_written_iov[i].iov_base, "ETag:", 5) == 0) {
      found_etag = 1;
      break;
    }
  }
  assert(found_etag);

  http_server_destroy(server);
  printf("  PASS: put_object_roundtrip\n");
}

static void test_delete_object_roundtrip(void) {
  mock_sock_reset();

  set_req("DELETE /bucket/key HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n");

  http_server_t *server = http_server_create(
    &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
  assert(server != NULL);

  http_server_poll(server);
  assert(server->conn_count == 1);

  /* should be 204 No Content */
  assert(mock_written_iov != NULL);
  assert(memcmp(mock_written_iov[1].iov_base, "204 No Content\r\n", 17) == 0);

  http_server_destroy(server);
  printf("  PASS: delete_object_roundtrip\n");
}

static void test_parse_error_returns_400(void) {
  mock_sock_reset();

  set_req("INVALID / HTTP/1.0\r\n\r\n");

  http_server_t *server = http_server_create(
    &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
  assert(server != NULL);

  http_server_poll(server);
  /* Connection is closed after error (keep_alive=false), so conn_count may be 0.
  * Only verify the response was written. */

  /* should be 400 Bad Request */
  assert(mock_written_iov != NULL);
  assert(memcmp(mock_written_iov[1].iov_base, "400 Bad Request\r\n", 18) == 0);

  http_server_destroy(server);
  printf("  PASS: parse_error_returns_400\n");
}

static void test_list_objects_roundtrip(void) {
  mock_sock_reset();

  set_req("GET /mybucket/?prefix=&max-keys=10 HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n");

  http_server_t *server = http_server_create(
    &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
  assert(server != NULL);

  http_server_poll(server);
  assert(server->conn_count == 1);

  assert(mock_written_iov != NULL);
  assert(memcmp(mock_written_iov[1].iov_base, "200 OK\r\n", 9) == 0);

  http_server_destroy(server);
  printf("  PASS: list_objects_roundtrip\n");
}

int main(void) {
  printf("test_http_integration:\n");
  test_get_object_roundtrip();
  test_put_object_roundtrip();
  test_delete_object_roundtrip();
  test_parse_error_returns_400();
  test_list_objects_roundtrip();
  printf("  ALL PASS\n");
  return 0;
}
