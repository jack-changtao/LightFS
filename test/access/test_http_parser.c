#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "http_parser.h"

static void test_parse_simple_get(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req =
        "GET /mybucket/mykey.txt HTTP/1.1\r\n"
        "Host: s3.example.com\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    int consumed = http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(consumed == (int)strlen(req));
    assert(!ctx.error_flag);
    assert(ctx.request.complete);
    assert(ctx.request.keep_alive);
    assert(strcmp(ctx.request.method, "GET") == 0);
    assert(strcmp(ctx.request.uri, "/mybucket/mykey.txt") == 0);
    assert(ctx.request.body_len == 0);

    http_parser_reset(&ctx);
    printf("  PASS: parse_simple_get\n");
}

static void test_parse_put_with_body(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req =
        "PUT /bucket/key HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 12\r\n"
        "\r\n"
        "hello world!";

    int consumed = http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(consumed == (int)strlen(req));
    assert(!ctx.error_flag);
    assert(ctx.request.complete);
    assert(strcmp(ctx.request.method, "PUT") == 0);
    assert(ctx.request.body_len == 12);
    assert(memcmp(ctx.request.body, "hello world!", 12) == 0);
    assert(ctx.header_count == 3);
    http_request_destroy(&ctx.request);
    printf("  PASS: parse_put_with_body\n");
}

static void test_parse_s3_headers(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req =
        "GET /b/k HTTP/1.1\r\n"
        "Host: myhost\r\n"
        "Authorization: AWS4-HMAC-SHA256 Credential=AKID...\r\n"
        "X-Amz-Date: 20260518T120000Z\r\n"
        "\r\n";

    http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(ctx.request.complete);
    assert(strcmp(ctx.request.s3.host, "myhost") == 0);
    assert(strcmp(ctx.request.s3.authorization,
                  "AWS4-HMAC-SHA256 Credential=AKID...") == 0);
    assert(strcmp(ctx.request.s3.date, "20260518T120000Z") == 0);
    assert(strcmp(ctx.request.method, "GET") == 0);

    http_parser_reset(&ctx);
    printf("  PASS: parse_s3_headers\n");
}

static void test_parse_chunked_body(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req =
        "PUT /b/k HTTP/1.1\r\n"
        "Host: h\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "chunk1\r\n"
        "6\r\n"
        "chunk2\r\n"
        "0\r\n"
        "\r\n";

    http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(ctx.request.complete);
    assert(ctx.request.body_len == 12);
    assert(memcmp(ctx.request.body, "chunk1chunk2", 12) == 0);

    http_request_destroy(&ctx.request);
    printf("  PASS: parse_chunked_body\n");
}

static void test_connection_close(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req =
        "GET /b/k HTTP/1.1\r\n"
        "Host: h\r\n"
        "Connection: close\r\n"
        "\r\n";

    http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(ctx.request.complete);
    assert(!ctx.request.keep_alive);

    http_parser_reset(&ctx);
    printf("  PASS: connection_close\n");
}

static void test_parse_invalid_method(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *req = "FOO /b/k HTTP/1.1\r\n\r\n";
    int consumed = http_parser_feed(&ctx, (const uint8_t *)req, strlen(req));
    assert(consumed == -1);
    assert(ctx.error_flag);
    assert(ctx.last_error == HPE_INVALID_METHOD);

    printf("  PASS: parse_invalid_method\n");
}

static void test_pipelined_requests(void) {
    http_parser_ctx_t ctx;
    http_parser_init(&ctx);

    const char *reqs =
        "GET /b/k1 HTTP/1.1\r\nHost: h\r\n\r\n"
        "GET /b/k2 HTTP/1.1\r\nHost: h\r\n\r\n";

    int consumed = http_parser_feed(&ctx, (const uint8_t *)reqs, strlen(reqs));
    /* First request consumed, second remains */
    assert(consumed > 0 && consumed < (int)strlen(reqs));
    assert(ctx.request.complete);
    assert(strcmp(ctx.request.uri, "/b/k1") == 0);

    /* Parse remaining bytes for second request */
    http_parser_reset(&ctx);
    consumed = http_parser_feed(&ctx,
        (const uint8_t *)(reqs + consumed),
        strlen(reqs) - (size_t)consumed);
    assert(consumed > 0);
    assert(ctx.request.complete);
    assert(strcmp(ctx.request.uri, "/b/k2") == 0);

    http_parser_reset(&ctx);
    printf("  PASS: pipelined_requests\n");
}

int main(void) {
    printf("test_http_parser:\n");
    test_parse_simple_get();
    test_parse_put_with_body();
    test_parse_s3_headers();
    test_parse_chunked_body();
    test_connection_close();
    test_parse_invalid_method();
    test_pipelined_requests();
    printf("  ALL PASS\n");
    return 0;
}
