#ifndef LIGHTFS_STORAGE_SERVER_H
#define LIGHTFS_STORAGE_SERVER_H

#include <stdint.h>
#include <spdk/thread.h>

/* RPC opcodes */
#define RPC_OP_CREATE    0x10
#define RPC_OP_DELETE    0x11
#define RPC_OP_WRITE     0x12
#define RPC_OP_READ      0x13
#define RPC_OP_TRUNCATE  0x14
#define RPC_OP_PUNCH     0x15
#define RPC_OP_CLONE     0x16
#define RPC_OP_STATFS     0x17

typedef struct storage_server storage_server_t;
typedef struct obj_manager  obj_manager_t;
struct nrpc_transport;

storage_server_t *storage_server_create(struct spdk_thread *rpc_thread,
                                         struct nrpc_transport *transport,
                                         obj_manager_t *obj_mgr);
int  storage_server_start(storage_server_t *srv, const char *host, uint16_t port);
void storage_server_poll(storage_server_t *srv);
void storage_server_destroy(storage_server_t *srv);

#endif
