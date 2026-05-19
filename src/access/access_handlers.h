#ifndef LIGHTFS_ACCESS_HANDLERS_H
#define LIGHTFS_ACCESS_HANDLERS_H

#include "lightfs/access/access_types.h"

int s3_handler_put(const s3_request_t *request, const void *body, uint32_t body_length,
          s3_response_t *response);
int s3_handler_get(const s3_request_t *request, s3_response_t *response);
int s3_handler_delete(const s3_request_t *request, s3_response_t *response);
int s3_handler_list(const s3_request_t *request, s3_response_t *response);

#endif /* LIGHTFS_ACCESS_HANDLERS_H */
