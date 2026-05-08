#include "access_routes.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static s3_operation_t method_to_op(const char *method) {
    if (strcmp(method, "PUT") == 0) return S3_OP_PUT_OBJECT;
    if (strcmp(method, "GET") == 0) return S3_OP_GET_OBJECT;
    if (strcmp(method, "DELETE") == 0) return S3_OP_DELETE_OBJECT;
    if (strcmp(method, "HEAD") == 0) return S3_OP_HEAD_OBJECT;
    return S3_OP_UNKNOWN;
}

static int parse_path_style(const char *uri, s3_request_t *req) {
    if (uri[0] != '/') return -1;

    const char *query = strchr(uri, '?');
    const char *after_bucket = query ? query : uri + strlen(uri);

    /* Find slash only between '/' after bucket name and query (if any) */
    const char *slash = strchr(uri + 1, '/');
    if (slash && slash > after_bucket) {
        slash = NULL;  /* slash is in query string, ignore it */
    }

    if (slash) {
        size_t bucket_len = (size_t)(slash - (uri + 1));
        if (bucket_len > S3_MAX_BUCKET_LEN) return -1;
        strncpy(req->bucket, uri + 1, bucket_len);
        req->bucket[bucket_len] = '\0';

        const char *key_end = query ? query : (const char *)(uri + strlen(uri));
        size_t key_len = (size_t)(key_end - slash - 1);
        if (key_len > S3_MAX_KEY_LEN) return -1;
        strncpy(req->key, slash + 1, key_len);
        req->key[key_len] = '\0';
    } else {
        size_t len = (size_t)(after_bucket - (uri + 1));
        if (len > S3_MAX_BUCKET_LEN) return -1;
        strncpy(req->bucket, uri + 1, len);
        req->bucket[len] = '\0';
        req->key[0] = '\0';
    }
    return 0;
}

int s3_route_parse(const char *method, const char *uri,
                    const char *headers[], int header_count,
                    s3_request_t *req) {
    if (!method || !uri || !req) return -1;

    req->op = method_to_op(method);
    if (req->op == S3_OP_UNKNOWN) return -1;

    int rc = parse_path_style(uri, req);
    if (rc != 0) return -1;

    const char *query = strchr(uri, '?');
    if (query && req->key[0] == '\0') {
        if (strstr(query, "prefix=") || strstr(query, "max-keys=") ||
            strstr(query, "marker=") || strstr(query, "list-type=")) {
            req->op = S3_OP_LIST_OBJECTS;
        }
    }

    for (int i = 0; i < header_count; i += 2) {
        if (i + 1 >= header_count) break;
        if (strcmp(headers[i], "Content-Length") == 0) {
            req->content_length = (uint64_t)atoll(headers[i + 1]);
        } else if (strcmp(headers[i], "Content-Type") == 0) {
            strncpy(req->content_type, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "Authorization") == 0) {
            strncpy(req->authorization, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "Host") == 0) {
            strncpy(req->host, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "x-amz-date") == 0) {
            strncpy(req->date, headers[i + 1], S3_MAX_HEADER_LEN);
        }
    }

    return 0;
}

int s3_route_http_status(s3_operation_t op, int gateway_status) {
    (void)op;
    if (gateway_status == 0) return 200;
    if (gateway_status == -1) return 500;
    return 400;
}
