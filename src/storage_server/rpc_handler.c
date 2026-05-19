#include "storage_server_internal.h"
#include "frame.h"
#include <stdlib.h>
#include <string.h>

static bridge_ctx_t *
make_bridge_ctx(struct nrpc_server *rpc_server, uint64_t request_id,
                uint32_t opcode, struct spdk_thread *rpc_thread)
{
  bridge_ctx_t *bctx = calloc(1, sizeof(*bctx));
  if (!bctx) {
    return NULL;
  }
  bctx->rpc_server = rpc_server;
  bctx->request_id = request_id;
  bctx->opcode = opcode;
  bctx->rpc_thread = rpc_thread;
  return bctx;
}

static void
encode_simple_error(uint8_t *resp_buf, uint32_t *out_status, uint32_t *out_body_len,
                    io_result_t result)
{
  uint32_t st = result;
  memcpy(resp_buf, &st, 4);
  *out_status = NRPC_STATUS_OK;
  *out_body_len = 4;
}

/* --- CREATE (0x10): body = uint64_t object_id --- */
int
rpc_handler_create(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                   uint8_t *resp_buf, size_t resp_cap,
                   uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 8) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid;
  memcpy(&oid, req, 8);

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_CREATE, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  io_result_t rc = obj_create(srv->obj_mgr, oid, bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- DELETE (0x11): body = uint64_t object_id --- */
int
rpc_handler_delete(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                   uint8_t *resp_buf, size_t resp_cap,
                   uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 8) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid;
  memcpy(&oid, req, 8);

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_DELETE, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  io_result_t rc = obj_delete(srv->obj_mgr, oid, bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- WRITE (0x12): body = uint64_t oid + uint64_t offset + uint32_t len + data[] --- */
int
rpc_handler_write(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                  uint8_t *resp_buf, size_t resp_cap,
                  uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 20) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid, offset;
  uint32_t len;
  memcpy(&oid, req, 8);
  memcpy(&offset, (const uint8_t *)req + 8, 8);
  memcpy(&len, (const uint8_t *)req + 16, 4);

  if ((uint64_t)len + 20 != req_len) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_WRITE, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  const void *data = (const uint8_t *)req + 20;
  io_result_t rc = obj_write(srv->obj_mgr, oid, offset, len, (char *)data,
                              bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- READ (0x13): body = uint64_t oid + uint64_t offset + uint32_t len --- */
int
rpc_handler_read(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                 uint8_t *resp_buf, size_t resp_cap,
                 uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 20) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid, offset;
  uint32_t len;
  memcpy(&oid, req, 8);
  memcpy(&offset, (const uint8_t *)req + 8, 8);
  memcpy(&len, (const uint8_t *)req + 16, 4);

  char *read_buf = malloc(len);
  if (!read_buf) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_READ, srv->rpc_thread);
  if (!bctx) {
    free(read_buf);
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }
  bctx->extra_data = (uint8_t *)read_buf;
  bctx->extra_len = len;

  io_result_t rc = obj_read(srv->obj_mgr, oid, offset, len, read_buf,
                             bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(read_buf);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- TRUNCATE (0x14): body = uint64_t oid + uint64_t size --- */
int
rpc_handler_truncate(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                     uint8_t *resp_buf, size_t resp_cap,
                     uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 16) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid, size;
  memcpy(&oid, req, 8);
  memcpy(&size, (const uint8_t *)req + 8, 8);

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_TRUNCATE, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  io_result_t rc = obj_truncate(srv->obj_mgr, oid, size, bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- PUNCH (0x15): body = uint64_t oid + uint64_t offset + uint32_t len --- */
int
rpc_handler_punch(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                  uint8_t *resp_buf, size_t resp_cap,
                  uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 20) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t oid, offset;
  uint32_t len;
  memcpy(&oid, req, 8);
  memcpy(&offset, (const uint8_t *)req + 8, 8);
  memcpy(&len, (const uint8_t *)req + 16, 4);

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_PUNCH, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  io_result_t rc = obj_punch(srv->obj_mgr, oid, offset, len, bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- CLONE (0x16): body = uint64_t src_oid + uint64_t dst_oid --- */
int
rpc_handler_clone(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                  uint8_t *resp_buf, size_t resp_cap,
                  uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode;
  if (req_len < 16) {
    *out_status = NRPC_STATUS_BAD_OPCODE;
    *out_body_len = 0;
    return 0;
  }
  uint64_t src_oid, dst_oid;
  memcpy(&src_oid, req, 8);
  memcpy(&dst_oid, (const uint8_t *)req + 8, 8);

  bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
      nrpc_server_get_request_id(srv->nrpc), RPC_OP_CLONE, srv->rpc_thread);
  if (!bctx) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  io_result_t rc = obj_clone(srv->obj_mgr, src_oid, dst_oid, bridge_callback, bctx);
  if (rc != IO_SUCCESS) {
    encode_simple_error(resp_buf, out_status, out_body_len, rc);
    free(bctx);
    return 0;
  }

  return NRPC_HANDLER_DEFERRED;
}

/* --- STATFS (0x17): aggregates across all shards --- */

static void
collect_shard_statfs(void *arg)
{
  statfs_shard_ctx_t *sctx = arg;
  statfs_aggregator_t *agg = sctx->agg;
  uint64_t total = 0, used = 0;

  if (shard_get_statfs(sctx->shard, &total, &used) == 0) {
    __sync_fetch_and_add(&agg->total_size, total);
    __sync_fetch_and_add(&agg->used_size, used);
  } else {
    __sync_fetch_and_add(&agg->error_count, 1);
  }

  int rem = __sync_sub_and_fetch(&agg->remaining, 1);
  if (rem == 0) {
    reply_msg_t *msg = calloc(1, sizeof(*msg));
    if (!msg) {
      free(agg);
      free(sctx);
      return;
    }
    msg->rpc_server = agg->rpc_server;
    msg->request_id = agg->request_id;
    msg->rpc_thread = agg->rpc_thread;
    msg->result = (agg->error_count > 0) ? IO_ERROR : IO_SUCCESS;
    msg->opcode = RPC_OP_STATFS;

    uint8_t *extra = malloc(16);
    if (extra) {
      memcpy(extra, &agg->total_size, 8);
      memcpy(extra + 8, &agg->used_size, 8);
      msg->extra_data = extra;
      msg->extra_len = 16;
    }

    spdk_thread_send_msg(agg->rpc_thread, send_deferred_reply, msg);
    free(agg);
  }
  free(sctx);
}

int
rpc_handler_statfs(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                   uint8_t *resp_buf, size_t resp_cap,
                   uint32_t *out_status, uint32_t *out_body_len)
{
  storage_server_t *srv = ctx;
  (void)opcode; (void)req; (void)req_len;

  int num_shards = obj_manager_get_shard_count(srv->obj_mgr);
  if (num_shards == 0) {
    encode_simple_error(resp_buf, out_status, out_body_len, IO_ERROR);
    return 0;
  }

  shard_manager_t *shard_mgr = obj_manager_get_shard_manager(srv->obj_mgr);

  statfs_aggregator_t *agg = calloc(1, sizeof(*agg));
  if (!agg) {
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }
  agg->rpc_server = srv->nrpc;
  agg->request_id = nrpc_server_get_request_id(srv->nrpc);
  agg->rpc_thread = srv->rpc_thread;
  agg->remaining = num_shards;

  /* Pre-allocate all shard contexts before dispatching */
  statfs_shard_ctx_t **ctxs = calloc((size_t)num_shards, sizeof(*ctxs));
  if (!ctxs) {
    free(agg);
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  bool alloc_failed = false;
  for (int i = 0; i < num_shards; i++) {
    ctxs[i] = calloc(1, sizeof(statfs_shard_ctx_t));
    if (!ctxs[i]) {
      alloc_failed = true;
      break;
    }
  }

  if (alloc_failed) {
    for (int i = 0; i < num_shards; i++) {
      free(ctxs[i]);
    }
    free(ctxs);
    free(agg);
    *out_status = NRPC_STATUS_INTERNAL;
    *out_body_len = 0;
    return 0;
  }

  for (int i = 0; i < num_shards; i++) {
    ctxs[i]->agg = agg;
    ctxs[i]->shard = &shard_mgr->shards[i];
    spdk_thread_send_msg(shard_mgr->shards[i].thread,
                          collect_shard_statfs, ctxs[i]);
  }
  free(ctxs);

  return NRPC_HANDLER_DEFERRED;
}
