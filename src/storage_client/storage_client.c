#include "storage_client_internal.h"
#include "frame.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void client_ready_cb(void *ctx, int endpoint_index);
static void client_response_cb(void *ctx, uint64_t request_id, uint32_t status,
                                const void *body, uint32_t body_len, int transport_err);

/* --- Encode helpers --- */

static int
encode_create_req(uint64_t oid, uint8_t *buf, uint32_t cap)
{
  if (cap < 8) { return -1; }
  memcpy(buf, &oid, 8);
  return 8;
}

static int
encode_delete_req(uint64_t oid, uint8_t *buf, uint32_t cap)
{
  if (cap < 8) { return -1; }
  memcpy(buf, &oid, 8);
  return 8;
}

static int
encode_write_req(uint64_t oid, uint64_t offset, const void *data,
                  uint32_t len, uint8_t *buf, uint32_t cap)
{
  uint32_t total = 8 + 8 + 4 + len;
  if (cap < total) { return -1; }
  memcpy(buf, &oid, 8);
  memcpy(buf + 8, &offset, 8);
  memcpy(buf + 16, &len, 4);
  memcpy(buf + 20, data, len);
  return total;
}

static int
encode_read_req(uint64_t oid, uint64_t offset, uint32_t len,
                 uint8_t *buf, uint32_t cap)
{
  if (cap < 20) { return -1; }
  memcpy(buf, &oid, 8);
  memcpy(buf + 8, &offset, 8);
  memcpy(buf + 16, &len, 4);
  return 20;
}

static int
encode_truncate_req(uint64_t oid, uint64_t size, uint8_t *buf, uint32_t cap)
{
  if (cap < 16) { return -1; }
  memcpy(buf, &oid, 8);
  memcpy(buf + 8, &size, 8);
  return 16;
}

static int
encode_punch_req(uint64_t oid, uint64_t offset, uint32_t len,
                  uint8_t *buf, uint32_t cap)
{
  if (cap < 20) { return -1; }
  memcpy(buf, &oid, 8);
  memcpy(buf + 8, &offset, 8);
  memcpy(buf + 16, &len, 4);
  return 20;
}

static int
encode_clone_req(uint64_t src_oid, uint64_t dst_oid, uint8_t *buf, uint32_t cap)
{
  if (cap < 16) { return -1; }
  memcpy(buf, &src_oid, 8);
  memcpy(buf + 8, &dst_oid, 8);
  return 16;
}

/* --- Lifecycle --- */

storage_client_t *
storage_client_create(struct spdk_thread *thread, const char *host, uint16_t port)
{
  storage_client_t *c = calloc(1, sizeof(*c));
  if (!c) {
    return NULL;
  }
  c->host = strdup(host);
  c->port = port;
  c->is_connected = false;

  c->rpc_client = nrpc_client_create(thread, NULL);
  if (!c->rpc_client) {
    free(c->host);
    free(c);
    return NULL;
  }

  struct nrpc_transport *tcp = nrpc_tcp_transport_alloc();
  if (!tcp) {
    nrpc_client_destroy(c->rpc_client);
    free(c->host);
    free(c);
    return NULL;
  }

  int rc = nrpc_client_add_endpoint(c->rpc_client, tcp, host, port,
                                     client_ready_cb, c);
  if (rc < 0) {
    nrpc_tcp_transport_free(tcp);
    nrpc_client_destroy(c->rpc_client);
    free(c->host);
    free(c);
    return NULL;
  }
  c->endpoint_idx = rc;
  return c;
}

static void
client_ready_cb(void *ctx, int endpoint_index)
{
  storage_client_t *c = ctx;
  (void)endpoint_index;
  c->is_connected = true;
}

void
storage_client_destroy(storage_client_t *client)
{
  if (!client) {
    return;
  }
  nrpc_client_destroy(client->rpc_client);
  free(client->host);
  free(client);
}

void
storage_client_poll(storage_client_t *client)
{
  nrpc_client_poll(client->rpc_client);
}

/* --- RPC call helper --- */

static int
do_rpc_call(storage_client_t *c, uint32_t opcode, const uint8_t *body,
             uint32_t body_len, storage_pending_call_t *pcall)
{
  if (!c->is_connected) {
    return -ENOTCONN;
  }
  return nrpc_client_call(c->rpc_client, c->endpoint_idx,
                           NRPC_IFLAG_IDEMPOTENT, opcode,
                           body, body_len,
                           STORAGE_CLIENT_DEFAULT_TIMEOUT_MS,
                           client_response_cb, pcall);
}

/* --- Operations --- */

int
storage_client_create_obj(storage_client_t *c, uint64_t oid,
                           storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint8_t body[8];
  int len = encode_create_req(oid, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_CREATE, body, len, pcall);
}

int
storage_client_delete_obj(storage_client_t *c, uint64_t oid,
                           storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint8_t body[8];
  int len = encode_delete_req(oid, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_DELETE, body, len, pcall);
}

int
storage_client_write(storage_client_t *c, uint64_t oid, uint64_t offset,
                      const void *data, uint32_t len,
                      storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint32_t max_data = (1u << 20) - 20;
  if (len > max_data) {
    free(pcall);
    return -EINVAL;
  }

  uint8_t *body = malloc(20 + len);
  if (!body) {
    free(pcall);
    return -ENOMEM;
  }
  int blen = encode_write_req(oid, offset, data, len, body, 20 + len);
  int rc = do_rpc_call(c, RPC_OP_WRITE, body, blen, pcall);
  free(body);
  return rc;
}

int
storage_client_read(storage_client_t *c, uint64_t oid, uint64_t offset,
                     uint32_t len, storage_client_read_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_READ;
  pcall->callback.read = cb;
  pcall->user_ctx = ctx;

  uint8_t body[20];
  int blen = encode_read_req(oid, offset, len, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_READ, body, blen, pcall);
}

int
storage_client_truncate(storage_client_t *c, uint64_t oid, uint64_t size,
                         storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint8_t body[16];
  int blen = encode_truncate_req(oid, size, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_TRUNCATE, body, blen, pcall);
}

int
storage_client_punch(storage_client_t *c, uint64_t oid, uint64_t offset,
                      uint32_t len, storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint8_t body[20];
  int blen = encode_punch_req(oid, offset, len, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_PUNCH, body, blen, pcall);
}

int
storage_client_clone(storage_client_t *c, uint64_t src_oid, uint64_t dst_oid,
                      storage_client_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_SIMPLE;
  pcall->callback.simple = cb;
  pcall->user_ctx = ctx;

  uint8_t body[16];
  int blen = encode_clone_req(src_oid, dst_oid, body, sizeof(body));
  return do_rpc_call(c, RPC_OP_CLONE, body, blen, pcall);
}

int
storage_client_statfs(storage_client_t *c, storage_client_statfs_callback cb, void *ctx)
{
  storage_pending_call_t *pcall = calloc(1, sizeof(*pcall));
  pcall->type = CLIENT_CALL_STATFS;
  pcall->callback.statfs = cb;
  pcall->user_ctx = ctx;

  return do_rpc_call(c, RPC_OP_STATFS, NULL, 0, pcall);
}

/* --- Response callback --- */

static void
client_response_cb(void *ctx, uint64_t request_id, uint32_t status,
                    const void *body, uint32_t body_len, int transport_err)
{
  storage_pending_call_t *pcall = ctx;
  (void)request_id;

  if (transport_err != 0) {
    if (pcall->type == CLIENT_CALL_SIMPLE) {
      pcall->callback.simple(pcall->user_ctx, IO_ERROR);
    } else if (pcall->type == CLIENT_CALL_READ) {
      pcall->callback.read(pcall->user_ctx, IO_ERROR, NULL, 0);
    } else if (pcall->type == CLIENT_CALL_STATFS) {
      pcall->callback.statfs(pcall->user_ctx, IO_ERROR, 0, 0);
    }
    free(pcall);
    return;
  }

  if (body_len < 4) {
    if (pcall->type == CLIENT_CALL_SIMPLE) {
      pcall->callback.simple(pcall->user_ctx, IO_ERROR);
    } else if (pcall->type == CLIENT_CALL_READ) {
      pcall->callback.read(pcall->user_ctx, IO_ERROR, NULL, 0);
    } else if (pcall->type == CLIENT_CALL_STATFS) {
      pcall->callback.statfs(pcall->user_ctx, IO_ERROR, 0, 0);
    }
    free(pcall);
    return;
  }

  uint32_t io_status;
  memcpy(&io_status, body, 4);

  switch (pcall->type) {
  case CLIENT_CALL_SIMPLE:
    pcall->callback.simple(pcall->user_ctx, (io_result_t)io_status);
    break;
  case CLIENT_CALL_READ: {
    if (body_len >= 8) {
      uint32_t data_len;
      memcpy(&data_len, (const uint8_t *)body + 4, 4);
      const void *data = (const uint8_t *)body + 8;
      pcall->callback.read(pcall->user_ctx, (io_result_t)io_status, data, data_len);
    } else {
      pcall->callback.read(pcall->user_ctx, (io_result_t)io_status, NULL, 0);
    }
    break;
  }
  case CLIENT_CALL_STATFS: {
    if (body_len >= 20) {
      uint64_t total, used;
      memcpy(&total, (const uint8_t *)body + 4, 8);
      memcpy(&used, (const uint8_t *)body + 12, 8);
      pcall->callback.statfs(pcall->user_ctx, (io_result_t)io_status, total, used);
    } else {
      pcall->callback.statfs(pcall->user_ctx, (io_result_t)io_status, 0, 0);
    }
    break;
  }
  }
  free(pcall);
}
