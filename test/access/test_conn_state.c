#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "mock_spdk_sock.h"
#include "http_server.h"

static void test_conn_accept_and_read(void) {
    mock_sock_reset();
    mock_sock_set_recv((const uint8_t *)
        "GET /b/k HTTP/1.1\r\nHost: h\r\n\r\n", 34);

    http_server_t *server = http_server_create(
        &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
    assert(server != NULL);
    assert(server->conn_count == 0);

    http_server_poll(server);
    assert(server->conn_count == 1);

    http_conn_t *conn = TAILQ_FIRST(&server->conn_list);
    assert(conn != NULL);
    assert(strcmp(conn->parser_ctx.request.uri, "/b/k") == 0);
    assert(conn->parser_ctx.request.complete);

    http_server_destroy(server);
    printf("  PASS: conn_accept_and_read\n");
}

static void test_conn_keep_alive_cycle(void) {
    mock_sock_reset();
    mock_sock_set_recv((const uint8_t *)
        "GET /b/k1 HTTP/1.1\r\nHost: h\r\n\r\n", 33);

    http_server_t *server = http_server_create(
        &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
    http_server_poll(server);
    assert(server->conn_count == 1);

    http_conn_t *conn = TAILQ_FIRST(&server->conn_list);
    assert(conn->parser_ctx.request.complete);
    assert(conn->parser_ctx.request.keep_alive);

    http_conn_on_write_done(conn, 0);
    assert(conn->state == CONN_READING);

    http_server_destroy(server);
    printf("  PASS: conn_keep_alive_cycle\n");
}

static void test_conn_max_limit(void) {
    mock_sock_reset();
    http_server_t *server = http_server_create(
        &(http_server_config_t){
            .listen_host = "0.0.0.0",
            .listen_port = 8080,
            .max_connections = 0,
        });
    assert(server != NULL);

    http_server_poll(server);
    assert(server->conn_count == 0);

    http_server_destroy(server);
    printf("  PASS: conn_max_limit\n");
}

int main(void) {
    printf("test_conn_state:\n");
    test_conn_accept_and_read();
    test_conn_keep_alive_cycle();
    test_conn_max_limit();
    printf("  ALL PASS\n");
    return 0;
}
