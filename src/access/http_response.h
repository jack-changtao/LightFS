#ifndef LIGHTFS_HTTP_RESPONSE_H
#define LIGHTFS_HTTP_RESPONSE_H

#include "lightfs/access/access_types.h"
#include <stdbool.h>
#include <sys/uio.h>              /* struct iovec */

#define HTTP_IOV_MAX 16

/*
 * Serialize s3_response_t to an iovec array for spdk_sock_writev_async.
 * Static strings (status lines, header names) are pre-allocated — no snprintf.
 * response->body is referenced (not copied) in the iovec.
 * Returns number of iov entries written, or -1 on error.
 */
int http_response_serialize(const s3_response_t *response,
                            bool keep_alive,
                            struct iovec *iov, int max_iov);

#endif /* LIGHTFS_HTTP_RESPONSE_H */
