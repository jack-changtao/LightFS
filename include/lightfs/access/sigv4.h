#ifndef LIGHTFS_SIGV4_H
#define LIGHTFS_SIGV4_H

#include <stdint.h>

typedef enum {
  SIGV4_OK = 0,
  SIGV4_ERROR_MISSING_HEADER,
  SIGV4_ERROR_INVALID_CREDENTIAL,
  SIGV4_ERROR_INVALID_SIGNATURE,
  SIGV4_ERROR_EXPIRED,
  SIGV4_ERROR_INTERNAL,
} sigv4_result_t;

sigv4_result_t sigv4_validate(const char *auth_header,
               const char *method,
               const char *uri,
               const char *host,
               const char *date,
               const void *body,
               uint32_t body_length);

const char *sigv4_extract_access_key(const char *auth_header);

const char *sigv4_extract_date(const char *date_header);

#endif /* LIGHTFS_SIGV4_H */
