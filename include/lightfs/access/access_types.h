#ifndef LIGHTFS_ACCESS_TYPES_H
#define LIGHTFS_ACCESS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define S3_MAX_BUCKET_LENGTH 255
#define S3_MAX_KEY_LENGTH    1024
#define S3_MAX_HEADER_LENGTH 256

typedef enum {
  S3_OPERATION_PUT_OBJECT = 0,
  S3_OPERATION_GET_OBJECT,
  S3_OPERATION_DELETE_OBJECT,
  S3_OPERATION_HEAD_OBJECT,
  S3_OPERATION_LIST_OBJECTS,
  S3_OPERATION_PUT_BUCKET,
  S3_OPERATION_DELETE_BUCKET,
  S3_OPERATION_UNKNOWN,
} s3_operation_t;

typedef struct {
  char bucket[S3_MAX_BUCKET_LENGTH + 1];
  char key[S3_MAX_KEY_LENGTH + 1];
  s3_operation_t operation;
  uint64_t content_length;
  char content_type[S3_MAX_HEADER_LENGTH + 1];
  char authorization[S3_MAX_HEADER_LENGTH + 1];
  char host[S3_MAX_HEADER_LENGTH + 1];
  char date[S3_MAX_HEADER_LENGTH + 1];
  char sse_customer_algorithm[S3_MAX_HEADER_LENGTH + 1];
  char sse_customer_key[S3_MAX_HEADER_LENGTH + 1];
  char sse_customer_key_md5[S3_MAX_HEADER_LENGTH + 1];
} s3_request_t;

typedef struct {
  uint32_t http_status;
  const char *content_type;
  const void *body;
  uint32_t body_length;
  char etag[S3_MAX_HEADER_LENGTH + 1];
  char content_range[S3_MAX_HEADER_LENGTH + 1];
} s3_response_t;

typedef struct {
  char bucket[S3_MAX_BUCKET_LENGTH + 1];
  char key[S3_MAX_KEY_LENGTH + 1];
  uint64_t size;
  uint32_t checksum;
  uint64_t write_sequence;
  uint32_t datacenter_id;
  uint32_t fragment_count;
} object_manifest_t;

#endif /* LIGHTFS_ACCESS_TYPES_H */
