#ifndef LIGHTFS_STORAGE_CLIENT_H
#define LIGHTFS_STORAGE_CLIENT_H

#include <stdint.h>
#include "io_types.h"

struct spdk_thread;

typedef struct storage_client storage_client_t;

typedef void (*storage_client_callback)(void *ctx, io_result_t result);
typedef void (*storage_client_read_callback)(void *ctx, io_result_t result,
                                              const void *data, uint32_t len);
typedef void (*storage_client_statfs_callback)(void *ctx, io_result_t result,
                                                uint64_t total, uint64_t used);

storage_client_t *storage_client_create(struct spdk_thread *thread,
                                         const char *host, uint16_t port);
void storage_client_destroy(storage_client_t *client);
void storage_client_poll(storage_client_t *client);

int storage_client_create_obj(storage_client_t *c, uint64_t oid,
                               storage_client_callback cb, void *ctx);
int storage_client_delete_obj(storage_client_t *c, uint64_t oid,
                               storage_client_callback cb, void *ctx);
int storage_client_write(storage_client_t *c, uint64_t oid,
                          uint64_t offset, const void *data, uint32_t len,
                          storage_client_callback cb, void *ctx);
int storage_client_read(storage_client_t *c, uint64_t oid,
                         uint64_t offset, uint32_t len,
                         storage_client_read_callback cb, void *ctx);
int storage_client_truncate(storage_client_t *c, uint64_t oid,
                             uint64_t size, storage_client_callback cb, void *ctx);
int storage_client_punch(storage_client_t *c, uint64_t oid,
                          uint64_t offset, uint32_t len,
                          storage_client_callback cb, void *ctx);
int storage_client_clone(storage_client_t *c, uint64_t src_oid,
                          uint64_t dst_oid, storage_client_callback cb, void *ctx);
int storage_client_statfs(storage_client_t *c,
                           storage_client_statfs_callback cb, void *ctx);

#endif
