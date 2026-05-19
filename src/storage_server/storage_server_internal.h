#ifndef STORAGE_SERVER_INTERNAL_H
#define STORAGE_SERVER_INTERNAL_H

#include "lightfs/storage_server/storage_server.h"
#include "server.h"
#include "transport.h"
#include "io_types.h"
#include "obj.h"
#include "shard.h"
#include <spdk/thread.h>

/* Context passed to engine callback, forwarded to bridge */
typedef struct {
  struct nrpc_server    *rpc_server;
  uint64_t               request_id;
  uint32_t               opcode;
  struct spdk_thread    *rpc_thread;
  uint8_t               *extra_data;
  uint32_t               extra_len;
} bridge_ctx_t;

/* Reply message sent from shard thread to RPC thread via spdk_thread_send_msg */
typedef struct {
  struct nrpc_server    *rpc_server;
  uint64_t               request_id;
  struct spdk_thread    *rpc_thread;
  io_result_t            result;
  uint32_t               opcode;
  uint8_t               *extra_data;
  uint32_t               extra_len;
} reply_msg_t;

/* STATFS cross-shard aggregation state */
typedef struct {
  struct nrpc_server    *rpc_server;
  uint64_t               request_id;
  struct spdk_thread    *rpc_thread;
  int                    remaining;
  uint64_t               total_size;
  uint64_t               used_size;
  uint32_t               error_count;
} statfs_aggregator_t;

/* Per-shard context for collect_shard_statfs */
typedef struct {
  statfs_aggregator_t *agg;
  shard_t             *shard;
} statfs_shard_ctx_t;

struct storage_server {
  struct spdk_thread    *rpc_thread;
  struct nrpc_server    *nrpc;
  struct nrpc_transport *transport;
  obj_manager_t         *obj_mgr;
  char                  *host;
  uint16_t               port;
};

/* rpc_handler.c */
int rpc_handler_create(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                       uint8_t *resp_buf, size_t resp_cap,
                       uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_delete(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                       uint8_t *resp_buf, size_t resp_cap,
                       uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_write(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                      uint8_t *resp_buf, size_t resp_cap,
                      uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_read(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                     uint8_t *resp_buf, size_t resp_cap,
                     uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_truncate(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                         uint8_t *resp_buf, size_t resp_cap,
                         uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_punch(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                      uint8_t *resp_buf, size_t resp_cap,
                      uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_clone(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                      uint8_t *resp_buf, size_t resp_cap,
                      uint32_t *out_status, uint32_t *out_body_len);
int rpc_handler_statfs(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                       uint8_t *resp_buf, size_t resp_cap,
                       uint32_t *out_status, uint32_t *out_body_len);

/* callback_bridge.c */
void send_deferred_reply(void *arg);
void bridge_callback(void *user_ctx, io_result_t result);

#endif
