#include "storage_server_internal.h"
#include <stdlib.h>
#include <string.h>

storage_server_t *
storage_server_create(struct spdk_thread *rpc_thread,
                       struct nrpc_transport *transport,
                       obj_manager_t *obj_mgr)
{
  storage_server_t *srv = calloc(1, sizeof(*srv));
  if (!srv) {
    return NULL;
  }
  srv->rpc_thread = rpc_thread;
  srv->transport = transport;
  srv->obj_mgr = obj_mgr;

  srv->nrpc = nrpc_server_create(rpc_thread, transport);
  if (!srv->nrpc) {
    free(srv);
    return NULL;
  }

  return srv;
}

int
storage_server_start(storage_server_t *srv, const char *host, uint16_t port)
{
  if (nrpc_server_register(srv->nrpc, RPC_OP_CREATE,    rpc_handler_create,    srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_DELETE,    rpc_handler_delete,    srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_WRITE,     rpc_handler_write,     srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_READ,      rpc_handler_read,      srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_TRUNCATE,  rpc_handler_truncate,  srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_PUNCH,     rpc_handler_punch,     srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_CLONE,     rpc_handler_clone,     srv) < 0) { return -1; }
  if (nrpc_server_register(srv->nrpc, RPC_OP_STATFS,     rpc_handler_statfs,    srv) < 0) { return -1; }

  srv->host = strdup(host);
  srv->port = port;

  int rc = nrpc_server_listen(srv->nrpc, host, port);
  return rc;
}

void
storage_server_poll(storage_server_t *srv)
{
  nrpc_server_poll(srv->nrpc);
}

void
storage_server_destroy(storage_server_t *srv)
{
  if (!srv) {
    return;
  }
  nrpc_server_destroy(srv->nrpc);
  free(srv->host);
  free(srv);
}
