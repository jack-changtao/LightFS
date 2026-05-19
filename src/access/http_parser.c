#define _GNU_SOURCE
#include "http_parser.h"
#include <stdlib.h>
#include <string.h>

/* ── llhttp callbacks ────────────────────────────────────────────── */

static int on_message_begin(llhttp_t *parser) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;
    /* zero only HTTP-level fields; s3_request_t is populated by callbacks/s3_route_parse */
    memset(ctx->request.method, 0, sizeof(ctx->request.method));
    memset(ctx->request.uri, 0, sizeof(ctx->request.uri));
    ctx->request.body = NULL;
    ctx->request.body_len = 0;
    ctx->request.body_capacity = 0;
    ctx->request.complete = false;
    ctx->request.keep_alive = false;
    ctx->header_count = 0;
    ctx->parsing_header_value = false;
    ctx->error_flag = false;
    return 0;
}

static int on_url(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;
    size_t copy_len = len < HTTP_MAX_URI - 1 ? len : HTTP_MAX_URI - 1;
    memcpy(ctx->request.uri, data, copy_len);
    ctx->request.uri[copy_len] = '\0';
    return 0;
}

static int on_header_field(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;
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
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;
    if (ctx->header_count >= HTTP_MAX_HEADERS) return 0;

    char *name = ctx->header_names[ctx->header_count];
    char *val = ctx->header_values[ctx->header_count];

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
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;

    if (ctx->parsing_header_value && ctx->header_count < HTTP_MAX_HEADERS) {
        ctx->header_count++;
    }

    const char *method_str = llhttp_method_name(
        (llhttp_method_t)llhttp_get_method(parser));
    size_t mlen = strlen(method_str);
    memcpy(ctx->request.method, method_str,
           mlen < HTTP_METHOD_MAX - 1 ? mlen : HTTP_METHOD_MAX - 1);
    ctx->request.method[mlen < HTTP_METHOD_MAX ? mlen : HTTP_METHOD_MAX - 1] = '\0';

    ctx->request.keep_alive = llhttp_should_keep_alive(parser);

    uint64_t content_length = parser->content_length;
    if (content_length > 0) {
        size_t alloc_size = content_length < HTTP_DEFAULT_BODY_BUF
                                ? (size_t)content_length : HTTP_DEFAULT_BODY_BUF;
        ctx->request.body = malloc(alloc_size);
        ctx->request.body_capacity = (uint32_t)alloc_size;
    }

    return 0;
}

static int on_body(llhttp_t *parser, const char *data, size_t len) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;

    if (ctx->max_body_size > 0 &&
        ctx->request.body_len + len > ctx->max_body_size) {
        ctx->error_flag = true;
        ctx->last_error = HPE_USER;
        return -1;
    }

    if (!ctx->request.body || ctx->request.body_len + len > ctx->request.body_capacity) {
        size_t new_cap = ctx->request.body_capacity ? ctx->request.body_capacity * 2
                                                     : HTTP_DEFAULT_BODY_BUF;
        while (ctx->request.body_len + len > new_cap) new_cap *= 2;
        char *new_buf = realloc(ctx->request.body, new_cap);
        if (!new_buf) {
            ctx->error_flag = true;
            return -1;
        }
        ctx->request.body = new_buf;
        ctx->request.body_capacity = (uint32_t)new_cap;
    }
    memcpy(ctx->request.body + ctx->request.body_len, data, len);
    ctx->request.body_len += (uint32_t)len;
    return 0;
}

static int on_message_complete(llhttp_t *parser) {
    http_parser_ctx_t *ctx = (http_parser_ctx_t *)parser->data;

    for (int i = 0; i < ctx->header_count; i++) {
        ctx->header_ptrs[i * 2] = ctx->header_names[i];
        ctx->header_ptrs[i * 2 + 1] = ctx->header_values[i];
    }

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
    return HPE_PAUSED;
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
    ctx->parser.data = ctx;
}

int http_parser_feed(http_parser_ctx_t *ctx, const uint8_t *data, size_t len) {
    llhttp_errno_t err = llhttp_execute(&ctx->parser, (const char *)data, len);
    if (err != HPE_OK && err != HPE_PAUSED) {
        ctx->error_flag = true;
        ctx->last_error = err;
        return -1;
    }
    const char *pos = llhttp_get_error_pos(&ctx->parser);
    if (pos) {
        return (int)(pos - (const char *)data);
    }
    /* HPE_OK with no position means parser is waiting for more data */
    return 0;
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
