#ifndef LIGHTFS_ACCESS_TYPES_H
#define LIGHTFS_ACCESS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define S3_MAX_BUCKET_LEN 255
#define S3_MAX_KEY_LEN    1024
#define S3_MAX_HEADER_LEN 256

typedef enum {
    S3_OP_PUT_OBJECT = 0,
    S3_OP_GET_OBJECT,
    S3_OP_DELETE_OBJECT,
    S3_OP_HEAD_OBJECT,
    S3_OP_LIST_OBJECTS,
    S3_OP_PUT_BUCKET,
    S3_OP_DELETE_BUCKET,
    S3_OP_UNKNOWN,
} s3_operation_t;

typedef struct {
    char bucket[S3_MAX_BUCKET_LEN + 1];
    char key[S3_MAX_KEY_LEN + 1];
    s3_operation_t op;
    uint64_t content_length;
    char content_type[S3_MAX_HEADER_LEN + 1];
    char authorization[S3_MAX_HEADER_LEN + 1];
    char host[S3_MAX_HEADER_LEN + 1];
    char date[S3_MAX_HEADER_LEN + 1];
    char sse_customer_algorithm[S3_MAX_HEADER_LEN + 1];
    char sse_customer_key[S3_MAX_HEADER_LEN + 1];
    char sse_customer_key_md5[S3_MAX_HEADER_LEN + 1];
} s3_request_t;

typedef struct {
    uint32_t http_status;
    const char *content_type;
    const void *body;
    uint32_t body_len;
    char etag[S3_MAX_HEADER_LEN + 1];
    char content_range[S3_MAX_HEADER_LEN + 1];
} s3_response_t;

typedef struct {
    char bucket[S3_MAX_BUCKET_LEN + 1];
    char key[S3_MAX_KEY_LEN + 1];
    uint64_t size;
    uint32_t crc;
    uint64_t write_seq;
    uint32_t dc_id;
    uint32_t fragment_count;
} object_manifest_t;

#endif /* LIGHTFS_ACCESS_TYPES_H */
