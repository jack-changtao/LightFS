#ifndef STORAGE_CLIENT_INTERNAL_H
#define STORAGE_CLIENT_INTERNAL_H

#include "lightfs/storage_client/storage_client.h"
#include "lightfs/storage_server/storage_server.h"
#include "client.h"
#include "transport_tcp.h"
#include "spdk/thread.h"

#define STORAGE_CLIENT_DEFAULT_TIMEOUT_MS 5000

typedef enum {
  CLIENT_CALL_SIMPLE,
  CLIENT_CALL_READ,
  CLIENT_CALL_STATFS,
} client_call_type_t;

typedef struct {
  client_call_type_t type;
  union {
    storage_client_callback       simple;
    storage_client_read_callback  read;
    storage_client_statfs_callback statfs;
  } callback;
  void *user_ctx;
} storage_pending_call_t;

struct storage_client {
  struct spdk_thread *thread;
  struct nrpc_client *rpc_client;
  int                 endpoint_idx;
  bool                is_connected;
  char               *host;
  uint16_t            port;
};

#endif
