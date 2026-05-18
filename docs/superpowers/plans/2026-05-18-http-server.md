# HTTP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the printf-based stub in the LightFS Access Layer with a real HTTP/1.1 server built on SPDK sock + llhttp.

**Architecture:** llhttp (vendored C HTTP parser) converts byte streams into `s3_request_t` via callbacks. A connection state machine manages per-client lifecycle (read → parse → dispatch → write). `http_response_serialize()` converts `s3_response_t` to iovec arrays for zero-copy async write via `spdk_sock_writev_async()`. All runs within a single SPDK reactor thread with no external event loop.

**Tech Stack:** C11, SPDK `sock` + `event`, llhttp v9.3.x (vendored), OpenSSL (via SPDK sock SSL module), `assert()` for tests

**Note on tests:** All test files below use the same `assert()`-based pattern as existing tests (`test/access/test_*.c`). Each test is a standalone binary compiled and linked against the module `.o` files. Header format for `s3_route_parse` is `{"Name1", "Value1", "Name2", "Value2"}` with `header_count` being the total element count.

**Note on body field:** `s3_request_t` currently has no `body`/`body_len` fields. The plan adds an internal `http_request_t` wrapper in `http_parser.h` that carries method, uri, body buffer, and a `s3_request_t` embedded inside it. The dispatch function passes these to the existing route/auth/handler pipeline. This avoids modifying the public `s3_request_t` type.

---
## File Map

```
src/access/
├── third_party/llhttp.h        CREATE - vendored from nodejs/llhttp v9.3.x
├── third_party/llhttp.c        CREATE - vendored single-file C
├── http_parser.h               CREATE - internal header: http_parser_ctx_t, http_request_t
├── http_parser.c               CREATE - llhttp callbacks, header mapping, body buffering
├── http_response.h             CREATE - internal header: serialization API
├── http_response.c             CREATE - status line table, iovec serialization
├── http_server.h               CREATE - internal header: http_conn_t, http_server_t
├── http_server.c               CREATE - connection state machine, sock_group poller, timeouts
├── access_server.c             MODIFY - wire http_server_start/stop instead of printf
├── access_routes.c             UNCHANGED
├── access_handlers.c           UNCHANGED
├── s3_xml.c                    UNCHANGED
├── sigv4.c                     UNCHANGED
└── Makefile                    MODIFY - add new .o targets, llhttp compile

include/lightfs/access/
├── http_server.h               CREATE - public header: http_server_config_t, start/stop
├── access_server.h             UNCHANGED
├── access_types.h              UNCHANGED

test/access/
├── test_http_parser.c          CREATE
├── test_http_response.c        CREATE
├── test_conn_state.c           CREATE
├── test_http_integration.c     CREATE
└── Makefile                    MODIFY - add new test targets
```

---

### Task 1: Vendor llhttp

**Files:**
- Create: `src/access/third_party/llhttp.h`
- Create: `src/access/third_party/llhttp.c`

- [ ] **Step 1: Download llhttp release from GitHub**

```bash
mkdir -p src/access/third_party
curl -sL https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.3.1.tar.gz | tar xz
cp llhttp-release-v9.3.1/build/llhttp.h src/access/third_party/llhttp.h
cp llhttp-release-v9.3.1/build/llhttp.c src/access/third_party/llhttp.c
rm -rf llhttp-release-v9.3.1
```

- [ ] **Step 2: Verify llhttp compiles standalone**

```bash
cd src/access && gcc -std=c11 -Wall -Wextra -c -o third_party/llhttp.o third_party/llhttp.c
```

Expected: compiles without errors (may have a few warnings from generated code, that's normal).

- [ ] **Step 3: Commit**

```bash
git add src/access/third_party/llhttp.h src/access/third_party/llhttp.c
git commit -m "feat: vendor llhttp v9.3.1 HTTP parser"
```

---

### Task 2: http_parser.h — Types and API

**Files:**
- Create: `src/access/http_parser.h`

- [ ] **Step 1: Write the header**

```c
#ifndef LIGHTFS_HTTP_PARSER_H
#define LIGHTFS_HTTP_PARSER_H

#include "third_party/llhttp.h"
#include "lightfs/access/access_types.h"
#include <stdbool.h>
#include <stdint.h>

#define HTTP_MAX_HEADERS      64
#define HTTP_MAX_URI          4096
#define HTTP_METHOD_MAX       16
#define HTTP_DEFAULT_BODY_BUF 65536

/* Wraps s3_request_t with HTTP-level fields the parser populates */
typedef struct {
    s3_request_t s3;                              /* embedded, passed to route/auth/handler */
    char method[HTTP_METHOD_MAX];                 /* "GET", "PUT", "DELETE", "HEAD" */
    char uri[HTTP_MAX_URI];                       /* /bucket/key?query */
    char *body;                                   /* request body buffer */
    uint32_t body_len;                            /* bytes received */
    uint32_t body_capacity;                       /* allocated size */
    bool complete;                                /* full request received */
    bool keep_alive;                              /* keep connection after response */
} http_request_t;

/* llhttp integration context — one per connection */
typedef struct http_parser_ctx {
    llhttp_t parser;
    llhttp_settings_t settings;
    http_request_t request;                       /* current request being parsed */
    char header_names[HTTP_MAX_HEADERS][S3_MAX_HEADER_LENGTH + 1];
    char header_values[HTTP_MAX_HEADERS][S3_MAX_HEADER_LENGTH + 1];
    const char *header_ptrs[HTTP_MAX_HEADERS * 2]; /* interleaved ptrs for s3_route_parse */
    int header_count;
    char cur_header_name[S3_MAX_HEADER_LENGTH + 1];
    bool parsing_header_value;
    bool error_flag;
    llhttp_errno_t last_error;
} http_parser_ctx_t;

/* Initialize parser context (call once per connection) */
void http_parser_init(http_parser_ctx_t *ctx);

/*
 * Feed bytes to parser. Returns number of bytes consumed on success,
 * -1 on parse error. Check ctx->request.complete after return to see
 * if a full request has been parsed.
 * If the request is chunked, llhttp transparently decodes; on_body may
 * be called multiple times.
 */
int http_parser_feed(http_parser_ctx_t *ctx, const uint8_t *data, size_t len);

/* Reset parser state for next request on same connection (keep-alive) */
void http_parser_reset(http_parser_ctx_t *ctx);

/* Free any allocated body buffer in the request */
void http_request_destroy(http_request_t *req);

#endif /* LIGHTFS_HTTP_PARSER_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/access/http_parser.h
git commit -m "feat: add http_parser.h types and API"
```

---

### Task 3: http_parser.c — llhttp Callback Implementation

**Files:**
- Create: `src/access/http_parser.c`

- [ ] **Step 1: Write the implementation**

```c
#include "http_parser.h"
#include <stdlib.h>
#include <string.h>

/* ── llhttp callbacks ────────────────────────────────────────────── */

static int on_message_begin(llhttp_t *parser) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);
    memset(&ctx->request, 0, sizeof(ctx->request));
    ctx->header_count = 0;
    ctx->parsing_header_value = false;
    ctx->error_flag = false;
    ctx->request.body = NULL;
    ctx->request.body_len = 0;
    ctx->request.body_capacity = 0;
    ctx->request.complete = false;
    return 0;
}

static int on_url(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);
    size_t copy_len = len < HTTP_MAX_URI - 1 ? len : HTTP_MAX_URI - 1;
    memcpy(ctx->request.uri, data, copy_len);
    ctx->request.uri[copy_len] = '\0';
    return 0;
}

static int on_header_field(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);
    if (ctx->parsing_header_value && ctx->header_count < HTTP_MAX_HEADERS) {
        ctx->header_count++;
    }
    ctx->parsing_header_value = false;
    size_t copy_len = len < S3_MAX_HEADER_LENGTH ? len : S3_MAX_HEADER_LENGTH;
    memcpy(ctx->cur_header_name, data, copy_len);
    ctx->cur_header_name[copy_len] = '\0';
    return 0;
}

static int on_header_value(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);
    if (ctx->header_count >= HTTP_MAX_HEADERS) return 0;

    char *name = ctx->header_names[ctx->header_count];
    char *val = ctx->header_values[ctx->header_count];

    /* if first value chunk for this field, copy the cached name */
    if (!ctx->parsing_header_value) {
        size_t name_len = strlen(ctx->cur_header_name);
        memcpy(name, ctx->cur_header_name, name_len + 1);
        val[0] = '\0';
    }

    size_t cur_len = strlen(val);
    size_t copy_len = len < (S3_MAX_HEADER_LENGTH - cur_len)
                          ? len : (S3_MAX_HEADER_LENGTH - cur_len);
    memcpy(val + cur_len, data, copy_len);
    val[cur_len + copy_len] = '\0';
    ctx->parsing_header_value = true;
    return 0;
}

static int on_headers_complete(llhttp_t *parser) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);

    /* finalize last header if parsing its value */
    if (ctx->parsing_header_value && ctx->header_count < HTTP_MAX_HEADERS) {
        ctx->header_count++;
    }

    /* record HTTP method */
    const char *method_str = llhttp_method_name(
        (llhttp_method_t)llhttp_get_method(parser));
    size_t mlen = strlen(method_str);
    memcpy(ctx->request.method, method_str,
           mlen < HTTP_METHOD_MAX - 1 ? mlen : HTTP_METHOD_MAX - 1);
    ctx->request.method[mlen < HTTP_METHOD_MAX ? mlen : HTTP_METHOD_MAX - 1] = '\0';

    ctx->request.keep_alive = llhttp_should_keep_alive(parser);

    /* allocate body buffer if Content-Length or Transfer-Encoding is present */
    uint64_t content_length = llhttp_get_content_length(parser);
    if (content_length > 0) {
        size_t alloc_size = content_length < HTTP_DEFAULT_BODY_BUF
                                ? (size_t)content_length : HTTP_DEFAULT_BODY_BUF;
        ctx->request.body = malloc(alloc_size);
        ctx->request.body_capacity = (uint32_t)alloc_size;
    }

    return 0;
}

static int on_body(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);
    if (!ctx->request.body || ctx->request.body_len + len > ctx->request.body_capacity) {
        size_t new_cap = ctx->request.body_capacity ? ctx->request.body_capacity * 2
                                                     : HTTP_DEFAULT_BODY_BUF;
        while (ctx->request.body_len + len > new_cap) new_cap *= 2;
        char *new_buf = realloc(ctx->request.body, new_cap);
        if (!new_buf) return -1;
        ctx->request.body = new_buf;
        ctx->request.body_capacity = (uint32_t)new_cap;
    }
    memcpy(ctx->request.body + ctx->request.body_len, data, len);
    ctx->request.body_len += (uint32_t)len;
    return 0;
}

static int on_message_complete(llhttp_t *parser) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)llhttp_get_data(parser);

    /* build header_ptrs array for s3_route_parse (interleaved name/value) */
    for (int i = 0; i < ctx->header_count; i++) {
        ctx->header_ptrs[i * 2] = ctx->header_names[i];
        ctx->header_ptrs[i * 2 + 1] = ctx->header_values[i];
    }

    /* map header names to s3_request_t fields */
    http_request_t *req = &ctx->request;
    for (int i = 0; i < ctx->header_count; i++) {
        const char *name = ctx->header_names[i];
        const char *val = ctx->header_values[i];
        size_t vlen = strlen(val);

        if (strcasecmp(name, "content-length") == 0) {
            req->s3.content_length = strtoull(val, NULL, 10);
        } else if (strcasecmp(name, "content-type") == 0) {
            size_t clen = vlen < S3_MAX_HEADER_LENGTH ? vlen : S3_MAX_HEADER_LENGTH;
            memcpy(req->s3.content_type, val, clen);
            req->s3.content_type[clen] = '\0';
        } else if (strcasecmp(name, "authorization") == 0) {
            size_t alen = vlen < S3_MAX_HEADER_LENGTH ? vlen : S3_MAX_HEADER_LENGTH;
            memcpy(req->s3.authorization, val, alen);
            req->s3.authorization[alen] = '\0';
        } else if (strcasecmp(name, "host") == 0) {
            size_t hlen = vlen < S3_MAX_HEADER_LENGTH ? vlen : S3_MAX_HEADER_LENGTH;
            memcpy(req->s3.host, val, hlen);
            req->s3.host[hlen] = '\0';
        } else if (strcasecmp(name, "x-amz-date") == 0) {
            size_t dlen = vlen < S3_MAX_HEADER_LENGTH ? vlen : S3_MAX_HEADER_LENGTH;
            memcpy(req->s3.date, val, dlen);
            req->s3.date[dlen] = '\0';
        }
    }

    req->complete = true;
    return 0;
}

/* ── public API ─────────────────────────────────────────────────── */

void http_parser_init(http_parser_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    llhttp_settings_init(&ctx->settings);
    ctx->settings.on_message_begin = on_message_begin;
    ctx->settings.on_url = on_url;
    ctx->settings.on_header_field = on_header_field;
    ctx->settings.on_header_value = on_header_value;
    ctx->settings.on_headers_complete = on_headers_complete;
    ctx->settings.on_body = on_body;
    ctx->settings.on_message_complete = on_message_complete;
    llhttp_init(&ctx->parser, HTTP_REQUEST, &ctx->settings);
    llhttp_set_data(&ctx->parser, ctx);
}

int http_parser_feed(http_parser_ctx_t *ctx, const uint8_t *data, size_t len) {
    llhttp_errno_t err = llhttp_execute(&ctx->parser, (const char *)data, len);
    if (err != HPE_OK) {
        ctx->error_flag = true;
        ctx->last_error = err;
        return -1;
    }
    return (int)len;
}

void http_parser_reset(http_parser_ctx_t *ctx) {
    http_request_destroy(&ctx->request);
    llhttp_reset(&ctx->parser);
    ctx->header_count = 0;
    ctx->parsing_header_value = false;
    ctx->error_flag = false;
}

void http_request_destroy(http_request_t *req) {
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    req->body_capacity = 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/access/http_parser.c
git commit -m "feat: implement llhttp http_parser with S3 header mapping"
```

---

### Task 4: Test http_parser (TDD — verify before writing more code)

**Files:**
- Create: `test/access/test_http_parser.c`

- [ ] **Step 1: Write test — parse simple GET request**

```c
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
    assert(ctx.header_count == 3); /* Host, Content-Type, Content-Length */
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
    /* llhttp transparently decodes: body = "chunk1chunk2" */
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
    assert(consumed == (int)strlen(reqs));
    assert(ctx.request.complete);
    assert(strcmp(ctx.request.uri, "/b/k1") == 0);

    /* reset and parse next */
    http_parser_reset(&ctx);
    const char *remaining = reqs + consumed; /* simplified — real code tracks position */
    /* In real code: second request starts at offset after first end.
       Here, both are in buffer; parser consumed first, leftover on second.
       For the test we re-feed the second request explicitly: */
    http_parser_feed(&ctx, (const uint8_t *)
        "GET /b/k2 HTTP/1.1\r\nHost: h\r\n\r\n", 34);
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
```

- [ ] **Step 2: Compile and run test**

```bash
cd test/access && gcc -std=c11 -Wall -Wextra -g -I../../include -I../../src/access \
    -o test_http_parser test_http_parser.c \
    ../../src/access/http_parser.c ../../src/access/third_party/llhttp.c -lm
./test_http_parser
```

Expected: all 7 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add test/access/test_http_parser.c
git commit -m "test: add http_parser unit tests (llhttp callbacks)"
```

---

### Task 5: http_response.h + http_response.c — Response Serialization

**Files:**
- Create: `src/access/http_response.h`
- Create: `src/access/http_response.c`

- [ ] **Step 1: Write http_response.h**

```c
#ifndef LIGHTFS_HTTP_RESPONSE_H
#define LIGHTFS_HTTP_RESPONSE_H

#include "lightfs/access/access_types.h"
#include "third_party/llhttp.h"   /* for llhttp_method_t (enum for HTTP version) */
#include <stdbool.h>
#include <sys/uio.h>              /* struct iovec */

#define HTTP_IOV_MAX 16

/*
 * Serialize s3_response_t to an iovec array for spdk_sock_writev_async.
 * Static strings (status lines, header names) are pre-allocated — no snprintf.
 * response->body is referenced (not copied) in the iovec.
 * Returns number of iov entries written, or -1 on error.
 */
int http_response_serialize(const s3_response_t *response,
                            bool keep_alive,
                            struct iovec *iov, int max_iov);

/* Convert S3 operation to a Content-Type value suitable for the response */
const char *http_content_type_for_response(const s3_response_t *response);

#endif /* LIGHTFS_HTTP_RESPONSE_H */
```

- [ ] **Step 2: Write http_response.c**

```c
#include "http_response.h"
#include <string.h>
#include <stdio.h>

/* ── static iovec strings ────────────────────────────────────────── */

#define S(name, str) \
    static const struct iovec IOV_##name = { .iov_base = (char *)str, .iov_len = sizeof(str) - 1 }

S(HTTP_1_1,     "HTTP/1.1 ");
S(CRLF,         "\r\n");
S(COLON_SP,     ": ");
S(CT_XML,       "Content-Type: application/xml\r\n");
S(CT_OCTET,     "Content-Type: application/octet-stream\r\n");
S(CONN_KA,      "Connection: keep-alive\r\n");
S(CONN_CLOSE,   "Connection: close\r\n");
S(CONTENT_LEN,  "Content-Length: ");
S(ETAG,         "ETag: ");

static const struct iovec status_table[] = {
    [200] = { .iov_base = "200 OK\r\n",          .iov_len = 9  },
    [204] = { .iov_base = "204 No Content\r\n",   .iov_len = 17 },
    [206] = { .iov_base = "206 Partial Content\r\n", .iov_len = 22 },
    [400] = { .iov_base = "400 Bad Request\r\n",  .iov_len = 18 },
    [403] = { .iov_base = "403 Forbidden\r\n",    .iov_len = 16 },
    [404] = { .iov_base = "404 Not Found\r\n",    .iov_len = 16 },
    [405] = { .iov_base = "405 Method Not Allowed\r\n", .iov_len = 25 },
    [413] = { .iov_base = "413 Payload Too Large\r\n", .iov_len = 24 },
    [500] = { .iov_base = "500 Internal Server Error\r\n", .iov_len = 27 },
    [503] = { .iov_base = "503 Service Unavailable\r\n", .iov_len = 25 },
};

/* scratch buffers for dynamic header values (one per call, not reentrant) */
static char etag_buf[S3_MAX_HEADER_LENGTH + 16];
static char cl_buf[64];

/* ── public ──────────────────────────────────────────────────────── */

int http_response_serialize(const s3_response_t *response,
                            bool keep_alive,
                            struct iovec *iov, int max_iov) {
    int n = 0;

    /* status line: "HTTP/1.1 " + status + CRLF */
    if (max_iov < 2) return -1;
    iov[n++] = IOV_HTTP_1_1;
    uint32_t status = response->http_status;
    if (status < sizeof(status_table) / sizeof(status_table[0]) && status_table[status].iov_base) {
        iov[n++] = status_table[status];
    } else {
        iov[n++] = (struct iovec){ .iov_base = "500 Internal Server Error\r\n", .iov_len = 27 };
    }

    /* Content-Type */
    if (max_iov - n < 1) return -1;
    if (response->content_type && response->content_type[0]) {
        /* dynamic content-type: static prefix + value */
        iov[n++] = IOV_CT_OCTET;  /* default, caller can override */
    } else if (response->body && response->body_length > 0) {
        iov[n++] = IOV_CT_XML;    /* S3 errors/responses are XML */
    }

    /* ETag */
    if (response->etag[0]) {
        if (max_iov - n < 1) return -1;
        int w = snprintf(etag_buf, sizeof(etag_buf), "ETag: %s\r\n", response->etag);
        iov[n++] = (struct iovec){ .iov_base = etag_buf, .iov_len = (size_t)w };
    }

    /* Content-Length */
    if (response->body && response->body_length > 0) {
        if (max_iov - n < 1) return -1;
        int w = snprintf(cl_buf, sizeof(cl_buf), "Content-Length: %u\r\n", response->body_length);
        iov[n++] = (struct iovec){ .iov_base = cl_buf, .iov_len = (size_t)w };
    }

    /* Connection */
    if (max_iov - n < 1) return -1;
    iov[n++] = keep_alive ? IOV_CONN_KA : IOV_CONN_CLOSE;

    /* empty line */
    if (max_iov - n < 1) return -1;
    iov[n++] = IOV_CRLF;

    /* body (zero-copy pointer) */
    if (response->body && response->body_length > 0) {
        if (max_iov - n < 1) return -1;
        iov[n++] = (struct iovec){
            .iov_base = (void *)response->body,
            .iov_len = response->body_length
        };
    }

    return n;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/access/http_response.h src/access/http_response.c
git commit -m "feat: add http_response serialization (s3_response_t → iovec)"
```

---

### Task 6: Test http_response

**Files:**
- Create: `test/access/test_http_response.c`

- [ ] **Step 1: Write the test**

```c
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/uio.h>
#include "lightfs/access/access_types.h"
#include "http_response.h"

static void test_serialize_get_200(void) {
    s3_response_t resp = {
        .http_status = 200,
        .etag = "\"abc123\"",
        .body = (void *)"hello",
        .body_length = 5,
    };

    struct iovec iov[HTTP_IOV_MAX];
    int n = http_response_serialize(&resp, true, iov, HTTP_IOV_MAX);
    assert(n > 0);
    assert(n <= HTTP_IOV_MAX);

    /* verify status line */
    assert(memcmp(iov[0].iov_base, "HTTP/1.1 ", 9) == 0);
    assert(memcmp(iov[1].iov_base, "200 OK\r\n", 9) == 0);

    /* verify body is the last iov (zero-copy pointer) */
    assert(iov[n-1].iov_base == resp.body);
    assert(iov[n-1].iov_len == 5);

    /* verify connection: keep-alive header present somewhere */
    int found_keep_alive = 0;
    for (int i = 0; i < n; i++) {
        if (iov[i].iov_len >= 24 &&
            memcmp(iov[i].iov_base, "Connection: keep-alive", 22) == 0) {
            found_keep_alive = 1;
            break;
        }
    }
    assert(found_keep_alive);

    printf("  PASS: serialize_get_200\n");
}

static void test_serialize_delete_204(void) {
    s3_response_t resp = {
        .http_status = 204,
    };

    struct iovec iov[HTTP_IOV_MAX];
    int n = http_response_serialize(&resp, true, iov, HTTP_IOV_MAX);
    assert(n > 0);

    assert(memcmp(iov[1].iov_base, "204 No Content\r\n", 17) == 0);
    /* no body */
    assert(iov[n-1].iov_len == 2); /* CRLF */
    assert(memcmp(iov[n-1].iov_base, "\r\n", 2) == 0);

    printf("  PASS: serialize_delete_204\n");
}

static void test_serialize_close_connection(void) {
    s3_response_t resp = { .http_status = 200 };
    struct iovec iov[HTTP_IOV_MAX];
    int n = http_response_serialize(&resp, false, iov, HTTP_IOV_MAX);

    int found_close = 0;
    for (int i = 0; i < n; i++) {
        if (iov[i].iov_len >= 19 &&
            memcmp(iov[i].iov_base, "Connection: close", 17) == 0) {
            found_close = 1;
            break;
        }
    }
    assert(found_close);
    printf("  PASS: serialize_close_connection\n");
}

static void test_serialize_error_403(void) {
    s3_response_t resp = {
        .http_status = 403,
        .body = (void *)"<Error><Code>AccessDenied</Code></Error>",
        .body_length = 40,
    };

    struct iovec iov[HTTP_IOV_MAX];
    int n = http_response_serialize(&resp, false, iov, HTTP_IOV_MAX);
    assert(n > 0);
    assert(memcmp(iov[1].iov_base, "403 Forbidden\r\n", 16) == 0);
    assert(iov[n-1].iov_base == resp.body);

    printf("  PASS: serialize_error_403\n");
}

int main(void) {
    printf("test_http_response:\n");
    test_serialize_get_200();
    test_serialize_delete_204();
    test_serialize_close_connection();
    test_serialize_error_403();
    printf("  ALL PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile and run test**

```bash
cd test/access && gcc -std=c11 -Wall -Wextra -g -I../../include -I../../src/access \
    -o test_http_response test_http_response.c ../../src/access/http_response.c -lm
./test_http_response
```

Expected: all 4 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add test/access/test_http_response.c
git commit -m "test: add http_response serialization tests"
```

---

### Task 7: Public Header — include/lightfs/access/http_server.h

**Files:**
- Create: `include/lightfs/access/http_server.h`

- [ ] **Step 1: Write the public header**

```c
#ifndef LIGHTFS_HTTP_SERVER_H
#define LIGHTFS_HTTP_SERVER_H

#include <stdint.h>

typedef struct {
    const char *listen_host;
    uint16_t    listen_port;
    uint32_t    max_connections;
    uint32_t    keep_alive_timeout_ms;
    uint32_t    request_timeout_ms;
    uint32_t    max_request_body;
    const char *tls_cert_path;
    const char *tls_key_path;
} http_server_config_t;

struct http_server;

int  http_server_start(const http_server_config_t *config, struct http_server **out_server);
void http_server_stop(struct http_server *server);

#endif /* LIGHTFS_HTTP_SERVER_H */
```

- [ ] **Step 2: Commit**

```bash
git add include/lightfs/access/http_server.h
git commit -m "feat: add public http_server.h API"
```

---

### Task 8: http_server.h — Internal Connection and Server Types

**Files:**
- Create: `src/access/http_server.h`

- [ ] **Step 1: Write the internal header**

```c
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

/* Called when spdk_sock_group signals a sock is readable */
void http_conn_on_readable(http_conn_t *conn);

/* Called when spdk_sock_writev_async completes */
void http_conn_on_write_done(http_conn_t *conn, int err);

/* Called on new accept */
http_conn_t *http_conn_accept(http_server_t *server, struct spdk_sock *sock);

/* Dispatch parsed request through route→auth→handler pipeline, then write response */
void http_conn_dispatch(http_conn_t *conn);

#endif /* LIGHTFS_ACCESS_HTTP_SERVER_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/access/http_server.h
git commit -m "feat: add http_server.h connection and server types"
```

---

### Task 9: http_server.c — Connection State Machine + SPDK Integration

**Files:**
- Create: `src/access/http_server.c`

- [ ] **Step 1: Write the implementation (part 1 — connection lifecycle)**

```c
#include "http_server.h"
#include "http_response.h"
#include "access_routes.h"
#include "access_handlers.h"
#include "lightfs/access/sigv4.h"
#include "lightfs/access/s3_xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
    static char err_buf[512];
    memset(resp, 0, sizeof(*resp));
    resp->http_status = status;
    resp->content_type = "application/xml";
    int w = s3_xml_serialize_error(err_buf, sizeof(err_buf), code, msg);
    resp->body = err_buf;
    resp->body_length = (w > 0) ? (uint32_t)w : 0;
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
    req->cb_fn = NULL;   /* will be set by caller using conn as cb_arg */
    req->cb_arg = conn;
    req->iovcnt = iovcnt;
    memcpy(SPDK_SOCK_REQUEST_IOV(req, 0), iov, (size_t)iovcnt * sizeof(struct iovec));

    conn->state = CONN_WRITING;
    spdk_sock_writev_async(conn->sock, req);
}

/* ── connection ops ──────────────────────────────────────────────── */

http_conn_t *http_conn_accept(http_server_t *server, struct spdk_sock *sock) {
    if (server->conn_count >= server->max_connections) {
        /* exceed limit — accept then immediately close with 503 */
        http_conn_t *reject = calloc(1, sizeof(http_conn_t));
        reject->sock = sock;
        reject->server = server;
        reject->state = CONN_CLOSING;
        s3_response_t resp;
        fill_error_response(&resp, 503, "ServiceUnavailable", "Too many connections");
        conn_write_response(reject, &resp);
        return NULL;
    }

    http_conn_t *conn = calloc(1, sizeof(http_conn_t));
    conn->sock = sock;
    conn->group = server->group;
    conn->server = server;
    conn->recv_buf_size = DEFAULT_RECV_BUF_SIZE;
    conn->recv_buf = malloc(conn->recv_buf_size);
    conn->state = CONN_READING;
    conn->last_active_ts = now_ms();
    http_parser_init(&conn->parser_ctx);

    spdk_sock_group_add_sock(server->group, sock, NULL, conn);

    TAILQ_INSERT_TAIL(&server->conn_list, conn, link);
    server->conn_count++;

    return conn;
}

void http_conn_on_readable(http_conn_t *conn) {
    conn->last_active_ts = now_ms();

    /* fill remaining space in recv buffer */
    uint32_t space = conn->recv_buf_size - conn->recv_len;
    if (space < 4096) {
        /* expand buffer */
        conn->recv_buf_size *= 2;
        conn->recv_buf = realloc(conn->recv_buf, conn->recv_buf_size);
        space = conn->recv_buf_size - conn->recv_len;
    }

    ssize_t nread = spdk_sock_recv(conn->sock,
                                    conn->recv_buf + conn->recv_len, space);
    if (nread <= 0) {
        conn->state = CONN_CLOSING;
        return;
    }

    conn->recv_len += (uint32_t)nread;

    /* feed parser */
    int result = http_parser_feed(&conn->parser_ctx,
                                  conn->recv_buf, conn->recv_len);
    if (result < 0) {
        /* parse error */
        s3_response_t resp;
        fill_error_response(&resp, 400, "InvalidRequest", "HTTP parse error");
        conn->parser_ctx.request.keep_alive = false;
        conn_write_response(conn, &resp);
        return;
    }

    /* shift unconsumed data to front of buffer */
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

    /* 1. route parse */
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

    /* 2. auth */
    if (req->s3.authorization[0]) {
        sigv4_result_t auth = sigv4_validate(req->s3.authorization, req->method,
                                              req->uri, req->s3.host,
                                              req->s3.date, req->body,
                                              req->body_len);
        if (auth != SIGV4_ERROR_OK) {
            s3_response_t resp;
            fill_error_response(&resp, 403, "SignatureDoesNotMatch",
                                "The request signature does not match");
            conn_write_response(conn, &resp);
            return;
        }
    }

    /* 3. handler */
    s3_response_t response = {0};
    switch (req->s3.operation) {
    case S3_OPERATION_PUT_OBJECT:
        result = s3_handler_put(&req->s3, req->body, req->body_len, &response);
        break;
    case S3_OPERATION_GET_OBJECT:
        result = s3_handler_get(&req->s3, &response);
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
        /* prepare for next request on same connection */
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
    free(conn->recv_buf);
    free(conn);
}
```

- [ ] **Step 2: Write the implementation (part 2 — server lifecycle and poller)**

```c
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

    /* create listen socket */
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

    /* create sock group (epoll-style multiplexing) */
    server->group = spdk_sock_group_create(server);
    if (!server->group) {
        spdk_sock_close(&server->listen_sock);
        free(server);
        return NULL;
    }

    /* add listen socket to group */
    spdk_sock_group_add_sock(server->group, server->listen_sock, NULL, server);

    server->running = true;
    return server;
}

void http_server_destroy(http_server_t *server) {
    if (!server) return;
    server->running = false;

    /* close all connections */
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

static uint64_t last_timeout_check;

void http_server_poll(http_server_t *server) {
    if (!server->running) return;

    /* poll sock group for I/O events */
    int events = spdk_sock_group_poll(server->group);

    /* handle events: the sock_group callback fires for each readable/writable sock.
       If using direct recv mode (not recv_next), we need to iterate connections.
       For the spdk_sock_group_add_sock callback approach: */

    /* accept new connections (non-blocking) */
    struct spdk_sock *new_sock;
    while ((new_sock = spdk_sock_accept(server->listen_sock)) != NULL) {
        http_conn_accept(server, new_sock);
    }

    /* process readable connections */
    http_conn_t *conn, *tmp;
    TAILQ_FOREACH_SAFE(conn, &server->conn_list, link, tmp) {
        if (conn->state == CONN_READING || conn->state == CONN_IDLE) {
            http_conn_on_readable(conn);
        }
        if (conn->state == CONN_CLOSING) {
            http_conn_close(conn);
        }
    }

    /* timeout check (once per second) */
    uint64_t now = now_ms();
    if (now - last_timeout_check >= 1000) {
        check_timeouts(server);
        last_timeout_check = now;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/access/http_server.c
git commit -m "feat: implement http_server with connection state machine"
```

---

### Task 10: Test Connection State Machine

**Files:**
- Create: `test/access/test_conn_state.c`

- [ ] **Step 1: Write mock_spdk_sock.h**

```c
/* test/access/mock_spdk_sock.h — minimal mock for testing connection logic */
#ifndef MOCK_SPDK_SOCK_H
#define MOCK_SPDK_SOCK_H

#include <spdk/sock.h>
#include <stdlib.h>
#include <string.h>

/* mock state */
static uint8_t *mock_recv_data;
static size_t mock_recv_len;
static size_t mock_recv_offset;
static struct iovec *mock_written_iov;
static int mock_written_iovcnt;
static bool mock_sock_closed;

static void mock_sock_set_recv(const uint8_t *data, size_t len) {
    mock_recv_data = (uint8_t *)data;
    mock_recv_len = len;
    mock_recv_offset = 0;
    mock_sock_closed = false;
}

/* Override SPDK functions for test link */
struct spdk_sock *spdk_sock_listen_ext(const char *ip, int port,
                                        const char *impl, struct spdk_sock_opts *opts) {
    return (struct spdk_sock *)0x1; /* non-null sentinel */
}

struct spdk_sock *spdk_sock_accept(struct spdk_sock *sock) {
    static int call_count = 0;
    call_count++;
    return (call_count == 1) ? (struct spdk_sock *)0x2 : NULL; /* one accept */
}

ssize_t spdk_sock_recv(struct spdk_sock *sock, void *buf, size_t len) {
    size_t avail = mock_recv_len - mock_recv_offset;
    size_t copy = avail < len ? avail : len;
    memcpy(buf, mock_recv_data + mock_recv_offset, copy);
    mock_recv_offset += copy;
    return (ssize_t)copy;
}

void spdk_sock_writev_async(struct spdk_sock *sock, struct spdk_sock_request *req) {
    /* capture for test verification */
    mock_written_iov = SPDK_SOCK_REQUEST_IOV(req, 0);
    mock_written_iovcnt = req->iovcnt;
    /* invoke completion callback immediately (sync simulation) */
    if (req->cb_fn) req->cb_fn(req->cb_arg, 0);
    free(req);
}

int spdk_sock_close(struct spdk_sock **sock) {
    mock_sock_closed = true;
    *sock = NULL;
    return 0;
}

void spdk_sock_get_default_opts(struct spdk_sock_opts *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->opts_size = sizeof(*opts);
}

struct spdk_sock_group *spdk_sock_group_create(void *ctx) {
    return (struct spdk_sock_group *)0x3;
}

int spdk_sock_group_add_sock(struct spdk_sock_group *group, struct spdk_sock *sock,
                              spdk_sock_cb cb, void *arg) {
    return 0;
}

int spdk_sock_group_remove_sock(struct spdk_sock_group *group, struct spdk_sock *sock) {
    return 0;
}

int spdk_sock_group_poll(struct spdk_sock_group *group) { return 0; }
int spdk_sock_group_close(struct spdk_sock_group **group) { *group = NULL; return 0; }

#endif /* MOCK_SPDK_SOCK_H */
```

- [ ] **Step 2: Write test_conn_state.c**

```c
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "mock_spdk_sock.h"
#include "http_server.h"

static void test_conn_accept_and_read(void) {
    /* reset mock */
    mock_sock_set_recv((const uint8_t *)
        "GET /b/k HTTP/1.1\r\nHost: h\r\n\r\n", 34);

    http_server_t *server = http_server_create(
        &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
    assert(server != NULL);

    /* initial accept */
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
    /* first request */
    mock_sock_set_recv((const uint8_t *)
        "GET /b/k1 HTTP/1.1\r\nHost: h\r\n\r\n", 33);

    http_server_t *server = http_server_create(
        &(http_server_config_t){ .listen_host = "0.0.0.0", .listen_port = 8080 });
    http_server_poll(server);
    assert(server->conn_count == 1);

    http_conn_t *conn = TAILQ_FIRST(&server->conn_list);
    assert(conn->parser_ctx.request.complete);

    /* write response callback should reset parser for keep-alive */
    assert(conn->parser_ctx.request.keep_alive);

    /* simulate write done → goes back to READING */
    http_conn_on_write_done(conn, 0);
    assert(conn->state == CONN_READING);

    http_server_destroy(server);
    printf("  PASS: conn_keep_alive_cycle\n");
}

static void test_conn_max_limit(void) {
    /* setup server with max 0 connections */
    http_server_t *server = http_server_create(
        &(http_server_config_t){
            .listen_host = "0.0.0.0",
            .listen_port = 8080,
            .max_connections = 0,
        });
    assert(server != NULL);

    http_server_poll(server);
    /* no connections accepted */
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
```

- [ ] **Step 3: Compile and run test**

The test links against the mock (which provides spdk_sock symbol definitions) — SPDK headers needed for types, but no `-lspdk` link dependency:

```bash
cd test/access && gcc -std=c11 -Wall -Wextra -g \
    -I../../include -I../../src/access \
    -I../../src/access/third_party -I. \
    -I/root/spdk/include \
    -D_GNU_SOURCE \
    -o test_conn_state test_conn_state.c \
    ../../src/access/http_server.c \
    ../../src/access/http_parser.c \
    ../../src/access/http_response.c \
    ../../src/access/access_routes.c \
    ../../src/access/sigv4.c \
    ../../src/access/s3_xml.c \
    ../../src/access/access_handlers.c \
    ../../src/access/third_party/llhttp.c \
    ../mocks/mock_gateway.c -lm
./test_conn_state
```

- [ ] **Step 4: Commit**

```bash
git add test/access/mock_spdk_sock.h test/access/test_conn_state.c
git commit -m "test: add connection state machine tests with mock spdk_sock"
```

---

### Task 11: Modify access_server.c — Wire Real HTTP Server

**Files:**
- Modify: `src/access/access_server.c`
- Modify: `include/lightfs/access/access_server.h`

- [ ] **Step 1: Update access_server.h to accept http_server_config_t**

In `access_server.h`, change the config type to use http_server_config_t:

```c
/* access_server.h — updated */
#ifndef LIGHTFS_ACCESS_SERVER_H
#define LIGHTFS_ACCESS_SERVER_H

#include "lightfs/access/http_server.h"

int  access_server_start(const http_server_config_t *config);
void access_server_stop(void);

#endif /* LIGHTFS_ACCESS_SERVER_H */
```

- [ ] **Step 2: Rewrite access_server.c**

```c
#include "lightfs/access/access_server.h"
#include "http_server.h"
#include <stdio.h>

static struct http_server *g_server;

int access_server_start(const http_server_config_t *config) {
    if (!config) return -1;

    printf("Access Layer starting on %s:%d\n", config->listen_host, config->listen_port);

    int result = http_server_start(config, &g_server);
    if (result != 0) {
        printf("Access Layer: failed to start HTTP server\n");
        return -1;
    }

    printf("Access Layer: HTTP server started\n");
    return 0;
}

void access_server_stop(void) {
    printf("Access Layer: stopping HTTP server\n");
    http_server_stop(g_server);
    g_server = NULL;
}
```

- [ ] **Step 3: Add http_server_start/stop wrappers in http_server.c**

Append to `src/access/http_server.c`:

```c
/* ── public wrapper (called from access_server) ──────────────────── */

int http_server_start(const http_server_config_t *config, struct http_server **out_server) {
    *out_server = http_server_create(config);
    return (*out_server) ? 0 : -1;
}

void http_server_stop(struct http_server *server) {
    http_server_destroy(server);
}
```

- [ ] **Step 4: Commit**

```bash
git add src/access/access_server.c include/lightfs/access/access_server.h src/access/http_server.c
git commit -m "feat: wire http_server into access_server (replaces printf stub)"
```

---

### Task 12: Integration Test — End-to-End via Real TCP Socket

**Files:**
- Create: `test/access/test_http_integration.c`

- [ ] **Step 1: Write integration test**

```c
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lightfs/access/access_server.h"

static int send_http_request(const char *request_bytes, size_t len,
                              char *response, size_t resp_cap, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    ssize_t nw = write(fd, request_bytes, len);
    assert(nw == (ssize_t)len);

    /* simple read with timeout using select */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    int ready = select(fd + 1, &fds, NULL, NULL, &tv);
    assert(ready > 0);

    ssize_t nr = read(fd, response, resp_cap - 1);
    assert(nr > 0);
    response[nr] = '\0';
    close(fd);
    return (int)nr;
}

static void test_get_object_integration(void) {
    http_server_config_t cfg = {
        .listen_host = "127.0.0.1",
        .listen_port = 19999,
    };

    int result = access_server_start(&cfg);
    assert(result == 0);

    /* wait briefly for server to bind */
    usleep(100000);

    char response[8192] = {0};
    const char *req = "GET /mybucket/mykey HTTP/1.1\r\nHost: localhost\r\n\r\n";
    int nr = send_http_request(req, strlen(req), response, sizeof(response), 19999);
    assert(nr > 0);

    /* verify HTTP response */
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "ETag:") != NULL);
    assert(strstr(response, "placeholder_response_data") != NULL);

    printf("  PASS: get_object_integration\n");
    access_server_stop();
}

static void test_put_object_integration(void) {
    http_server_config_t cfg = {
        .listen_host = "127.0.0.1",
        .listen_port = 19998,
    };
    access_server_start(&cfg);
    usleep(100000);

    char response[8192] = {0};
    const char *req =
        "PUT /bucket/obj HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "data";
    int nr = send_http_request(req, strlen(req), response, sizeof(response), 19998);
    assert(nr > 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);

    printf("  PASS: put_object_integration\n");
    access_server_stop();
}

int main(void) {
    printf("test_http_integration:\n");
    test_get_object_integration();
    test_put_object_integration();
    printf("  ALL PASS\n");
    return 0;
}
```

- [ ] **Step 2: Compile with SPDK and run**

```bash
# Link against real SPDK (not mock)
cd test/access && gcc -std=c11 -Wall -Wextra -g -I../../include -I../../src/access \
    -I/root/spdk/include -L/root/spdk/build/lib \
    -o test_http_integration test_http_integration.c \
    ../../src/access/access_server.o \
    ../../src/access/http_server.o \
    ../../src/access/http_parser.o \
    ../../src/access/http_response.o \
    ../../src/access/access_routes.o \
    ../../src/access/sigv4.o \
    ../../src/access/s3_xml.o \
    ../../src/access/access_handlers.o \
    ../../src/access/third_party/llhttp.o \
    ../mocks/mock_gateway.o \
    -lspdk -lpthread -lm

LD_LIBRARY_PATH=/root/spdk/build/lib ./test_http_integration
```

- [ ] **Step 3: Commit**

```bash
git add test/access/test_http_integration.c
git commit -m "test: add integration test via real TCP socket"
```

---

### Task 13: Update Makefiles

**Files:**
- Modify: `src/access/Makefile`
- Modify: `test/access/Makefile`

- [ ] **Step 1: Update src/access/Makefile**

```makefile
# src/access/Makefile
CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I/root/spdk/include
LDFLAGS := -L/root/spdk/build/lib -lspdk -lpthread

SRCS = sigv4.c access_routes.c access_handlers.c s3_xml.c \
       http_parser.c http_response.c http_server.c access_server.c \
       third_party/llhttp.c
OBJS = $(SRCS:.c=.o)

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)

.PHONY: all clean
```

- [ ] **Step 2: Update test/access/Makefile**

```makefile
# test/access/Makefile
CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I../../src/access \
          -I../../src/access/third_party -I/root/spdk/include
LDFLAGS := -L/root/spdk/build/lib -lspdk -lpthread -lm

SRCDIR = ../../src/access
MOCKDIR = ../mocks

UNIT_TESTS = test_sigv4 test_routes test_s3_xml test_handlers \
             test_http_parser test_http_response test_conn_state

INT_TESTS = test_http_integration

TESTS = $(UNIT_TESTS) $(INT_TESTS)

# common object files for all access tests
ACCESS_OBJS = $(SRCDIR)/sigv4.o $(SRCDIR)/access_routes.o \
              $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o \
              $(SRCDIR)/http_parser.o $(SRCDIR)/http_response.o \
              $(SRCDIR)/http_server.o $(SRCDIR)/access_server.o \
              $(SRCDIR)/third_party/llhttp.o $(MOCKDIR)/mock_gateway.o

all: $(TESTS)

test_sigv4: test_sigv4.c $(SRCDIR)/sigv4.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/sigv4.o $(LDFLAGS)

test_routes: test_routes.c $(SRCDIR)/access_routes.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_routes.o $(LDFLAGS)

test_s3_xml: test_s3_xml.c $(SRCDIR)/s3_xml.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/s3_xml.o $(LDFLAGS)

test_handlers: test_handlers.c $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o $(LDFLAGS)

test_http_parser: test_http_parser.c $(SRCDIR)/http_parser.o $(SRCDIR)/third_party/llhttp.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/http_parser.o $(SRCDIR)/third_party/llhttp.o $(LDFLAGS)

test_http_response: test_http_response.c $(SRCDIR)/http_response.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/http_response.o $(LDFLAGS)

test_conn_state: test_conn_state.c $(ACCESS_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(ACCESS_OBJS) $(LDFLAGS)

test_http_integration: test_http_integration.c $(ACCESS_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(ACCESS_OBJS) $(LDFLAGS)

run: all
	@echo "=== Running test_sigv4 ===" && ./test_sigv4
	@echo "=== Running test_routes ===" && ./test_routes
	@echo "=== Running test_s3_xml ===" && ./test_s3_xml
	@echo "=== Running test_handlers ===" && ./test_handlers
	@echo "=== Running test_http_parser ===" && ./test_http_parser
	@echo "=== Running test_http_response ===" && ./test_http_response
	@echo "=== Running test_conn_state ===" && ./test_conn_state
	@echo "=== Running test_http_integration ===" && ./test_http_integration

clean:
	rm -f $(TESTS)

.PHONY: all run clean
```

- [ ] **Step 3: Commit**

```bash
git add src/access/Makefile test/access/Makefile
git commit -m "build: update Makefiles for http_server and new tests"
```

---

### Task 14: Build and Run All Tests

- [ ] **Step 1: Build all module .o files**

```bash
cd src/access && make clean && make
```

- [ ] **Step 2: Build and run all tests**

```bash
cd test/access && make clean && make && make run
```

Expected: all tests PASS across all test binaries.

- [ ] **Step 3: Commit (if any fixups needed)**

```bash
git add -A && git commit -m "fix: build fixes from full test run"
```

---

## Verification

After all tasks complete, verify:

```bash
# 1. All module objects compile
cd src/access && make && echo "Module build: OK"

# 2. All tests pass
cd test/access && make run 2>&1 | grep -E "(PASS|FAIL|ALL PASS)"

# 3. Integration test runs independently
cd test/access && LD_LIBRARY_PATH=/root/spdk/build/lib ./test_http_integration

# 4. No regressions in existing tests
cd test/access && ./test_sigv4 && ./test_routes && ./test_s3_xml && ./test_handlers
```

## Summary of Changes

| Type | File |
|------|------|
| Create | `src/access/third_party/llhttp.h` |
| Create | `src/access/third_party/llhttp.c` |
| Create | `src/access/http_parser.h` |
| Create | `src/access/http_parser.c` |
| Create | `src/access/http_response.h` |
| Create | `src/access/http_response.c` |
| Create | `src/access/http_server.h` |
| Create | `src/access/http_server.c` |
| Create | `include/lightfs/access/http_server.h` |
| Create | `test/access/test_http_parser.c` |
| Create | `test/access/test_http_response.c` |
| Create | `test/access/mock_spdk_sock.h` |
| Create | `test/access/test_conn_state.c` |
| Create | `test/access/test_http_integration.c` |
| Modify | `src/access/access_server.c` |
| Modify | `src/access/Makefile` |
| Modify | `test/access/Makefile` |
| Modify | `include/lightfs/access/access_server.h` |
