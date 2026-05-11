#ifndef LIGHTFS_ACCESS_ROUTES_H
#define LIGHTFS_ACCESS_ROUTES_H

#include "lightfs/access/access_types.h"

int s3_route_parse(const char *method, const char *uri,
                    const char *headers[], int header_count,
                    s3_request_t *request);

int s3_route_http_status(s3_operation_t operation, int gateway_status);

#endif /* LIGHTFS_ACCESS_ROUTES_H */
