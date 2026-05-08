#ifndef LIGHTFS_ACCESS_HANDLERS_H
#define LIGHTFS_ACCESS_HANDLERS_H

#include "lightfs/access/access_types.h"

int s3_handler_put(const s3_request_t *req, const void *body, uint32_t body_len,
                    s3_response_t *resp);
int s3_handler_get(const s3_request_t *req, s3_response_t *resp);
int s3_handler_delete(const s3_request_t *req, s3_response_t *resp);
int s3_handler_list(const s3_request_t *req, s3_response_t *resp);

#endif /* LIGHTFS_ACCESS_HANDLERS_H */
