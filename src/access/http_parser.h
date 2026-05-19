#ifndef LIGHTFS_HTTP_PARSER_H
#define LIGHTFS_HTTP_PARSER_H

#include <llhttp.h>
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
    uint32_t max_body_size;                         /* enforced in on_body */
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
