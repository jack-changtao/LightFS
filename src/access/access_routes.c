#include "access_routes.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static s3_operation_t method_to_operation(const char *method) {
    if (strcmp(method, "PUT") == 0) return S3_OPERATION_PUT_OBJECT;
    if (strcmp(method, "GET") == 0) return S3_OPERATION_GET_OBJECT;
    if (strcmp(method, "DELETE") == 0) return S3_OPERATION_DELETE_OBJECT;
    if (strcmp(method, "HEAD") == 0) return S3_OPERATION_HEAD_OBJECT;
    return S3_OPERATION_UNKNOWN;
}

static int parse_path_style(const char *uri, s3_request_t *request) {
    if (uri[0] != '/') return -1;

    const char *query = strchr(uri, '?');
    const char *after_bucket = query ? query : uri + strlen(uri);

    /* Find slash only between '/' after bucket name and query (if any) */
    const char *slash = strchr(uri + 1, '/');
    if (slash && slash > after_bucket) {
        slash = NULL;  /* slash is in query string, ignore it */
    }

    if (slash) {
        size_t bucket_length = (size_t)(slash - (uri + 1));
        if (bucket_length > S3_MAX_BUCKET_LENGTH) return -1;
        strncpy(request->bucket, uri + 1, bucket_length);
        request->bucket[bucket_length] = '\0';

        const char *key_end = query ? query : (const char *)(uri + strlen(uri));
        size_t key_length = (size_t)(key_end - slash - 1);
        if (key_length > S3_MAX_KEY_LENGTH) return -1;
        strncpy(request->key, slash + 1, key_length);
        request->key[key_length] = '\0';
    } else {
        size_t length = (size_t)(after_bucket - (uri + 1));
        if (length > S3_MAX_BUCKET_LENGTH) return -1;
        strncpy(request->bucket, uri + 1, length);
        request->bucket[length] = '\0';
        request->key[0] = '\0';
    }
    return 0;
}

int s3_route_parse(const char *method, const char *uri,
                    const char *headers[], int header_count,
                    s3_request_t *request) {
    if (!method || !uri || !request) return -1;

    request->operation = method_to_operation(method);
    if (request->operation == S3_OPERATION_UNKNOWN) return -1;

    int result = parse_path_style(uri, request);
    if (result != 0) return -1;

    const char *query = strchr(uri, '?');
    if (query && request->key[0] == '\0') {
        if (strstr(query, "prefix=") || strstr(query, "max-keys=") ||
            strstr(query, "marker=") || strstr(query, "list-type=")) {
            request->operation = S3_OPERATION_LIST_OBJECTS;
        }
    }

    for (int i = 0; i < header_count; i += 2) {
        if (i + 1 >= header_count) break;
        if (strcmp(headers[i], "Content-Length") == 0) {
            request->content_length = (uint64_t)atoll(headers[i + 1]);
        } else if (strcmp(headers[i], "Content-Type") == 0) {
            strncpy(request->content_type, headers[i + 1], S3_MAX_HEADER_LENGTH);
        } else if (strcmp(headers[i], "Authorization") == 0) {
            strncpy(request->authorization, headers[i + 1], S3_MAX_HEADER_LENGTH);
        } else if (strcmp(headers[i], "Host") == 0) {
            strncpy(request->host, headers[i + 1], S3_MAX_HEADER_LENGTH);
        } else if (strcmp(headers[i], "x-amz-date") == 0) {
            strncpy(request->date, headers[i + 1], S3_MAX_HEADER_LENGTH);
        }
    }

    return 0;
}

int s3_route_http_status(s3_operation_t operation, int gateway_status) {
    (void)operation;
    if (gateway_status == 0) return 200;
    if (gateway_status == -1) return 500;
    return 400;
}
