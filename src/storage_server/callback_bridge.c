#include "storage_server_internal.h"
#include "frame.h"
#include <stdlib.h>
#include <string.h>

void
send_deferred_reply(void *arg)
{
  reply_msg_t *msg = arg;
  uint8_t body[NRPC_MAX_BODY];
  uint32_t body_len = 0;

  switch (msg->opcode) {
  case RPC_OP_READ: {
    uint32_t status_be = msg->result;
    memcpy(body, &status_be, 4);
    if (msg->result == IO_SUCCESS && msg->extra_data && msg->extra_len > 0) {
      memcpy(body + 4, &msg->extra_len, 4);
      memcpy(body + 8, msg->extra_data, msg->extra_len);
      body_len = 8 + msg->extra_len;
    } else {
      uint32_t zero = 0;
      memcpy(body + 4, &zero, 4);
      body_len = 8;
    }
    break;
  }
  case RPC_OP_STATFS: {
    uint32_t status_be = msg->result;
    memcpy(body, &status_be, 4);
    if (msg->extra_data && msg->extra_len >= 16) {
      memcpy(body + 4, msg->extra_data, 16);
    } else {
      memset(body + 4, 0, 16);
    }
    body_len = 20;
    break;
  }
  default: {
    uint32_t status_be = msg->result;
    memcpy(body, &status_be, 4);
    body_len = 4;
    break;
  }
  }

  nrpc_server_send_response(msg->rpc_server, msg->request_id,
                             NRPC_STATUS_OK, body, body_len);

  free(msg->extra_data);
  free(msg);
}

void
bridge_callback(void *user_ctx, io_result_t result)
{
  bridge_ctx_t *bctx = user_ctx;

  reply_msg_t *msg = calloc(1, sizeof(*msg));
  msg->rpc_server = bctx->rpc_server;
  msg->request_id = bctx->request_id;
  msg->rpc_thread  = bctx->rpc_thread;
  msg->result      = result;
  msg->opcode      = bctx->opcode;
  msg->extra_data  = bctx->extra_data;
  msg->extra_len   = bctx->extra_len;

  spdk_thread_send_msg(bctx->rpc_thread, send_deferred_reply, msg);

  free(bctx);
}
