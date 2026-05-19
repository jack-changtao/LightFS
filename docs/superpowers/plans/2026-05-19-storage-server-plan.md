# Storage Server + Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Storage Server that wraps the engine behind RPC, plus a client library for remote storage access.

**Architecture:** RPC thread on a dedicated core, engine shard threads on their own cores. Server handlers call `obj_xxx()` async, bridge callbacks forward results back to RPC thread via `spdk_thread_send_msg`. Client library wraps `nrpc_client` with an async callback API mirroring the engine.

**Tech Stack:** C11, SPDK reactor model, nrpc binary RPC (TCP transport), engine obj_manager + shard

---

## File Structure

### New files (create)

| File | Responsibility |
|------|---------------|
| `include/lightfs/storage_server/storage_server.h` | Public server types and API |
| `include/lightfs/storage_client/storage_client.h` | Public client types and API |
| `src/storage_server/storage_server_internal.h` | Internal structs (server context, bridge context, reply msg) |
| `src/storage_server/storage_server.c` | Lifecycle: create, start, poll, destroy |
| `src/storage_server/rpc_handler.c` | 8 opcode handlers + STATFS aggregation |
| `src/storage_server/callback_bridge.c` | Bridge callback for shard→RPC thread reply delivery |
| `src/storage_server/Makefile` | Build libstorageserver.a |
| `src/storage_client/storage_client_internal.h` | Internal structs (client context, pending call) |
| `src/storage_client/storage_client.c` | All client logic: encode, call, decode, callback dispatch |
| `src/storage_client/Makefile` | Build libstorageclient.a |

### Modified files

| File | Change |
|------|--------|
| `src/rpc/src/server.h` | Add `NRPC_HANDLER_DEFERRED` constant, `nrpc_server_send_response()` declaration |
| `src/rpc/src/server.c` | Handle deferred return in `try_dispatch_one()`, add `nrpc_server_send_response()` |
| `src/engine/src/obj.h` | Add `obj_manager_get_shard_count()`, `obj_manager_get_shard_manager()` |
| `src/engine/src/obj.c` | Implement the two new accessors |
| `src/engine/src/storage_engine.h` | Add `rpc_core_index`, `rpc_port` fields to `storage_engine_ctx_t` |
| `src/engine/src/storage_engine.c` | Extend `validate_core_affinity()`, create RPC thread + storage server after mount init |
| `src/engine/Makefile` | Add storage_server include/lib paths |

---

### Task 1: Add deferred reply support to nrpc_server

**Files:**
- Modify: `src/rpc/src/server.h`
- Modify: `src/rpc/src/server.c`

- [ ] **Step 1: Add `NRPC_HANDLER_DEFERRED` and `nrpc_server_send_response` to server.h**

In `src/rpc/src/server.h`, add after existing handler typedef:

```c
/* Handler returns this to defer response; caller will send via nrpc_server_send_response */
#define NRPC_HANDLER_DEFERRED 1

/**
 * Send a response for a previously deferred request.
 * Must be called from the server's SPDK thread.
 * The server takes ownership of body during the call (copies if needed).
 */
int nrpc_server_send_response(struct nrpc_server *server, uint64_t request_id,
                               uint32_t status, const void *body, uint32_t body_len);
```

- [ ] **Step 2: Implement deferred dispatch and send_response in server.c**

In `src/rpc/src/server.c`, modify `try_dispatch_one`:

The function currently calls the handler, then if the handler returns non-zero, it overrides the status to `NRPC_STATUS_INTERNAL`. Then it always encodes and sends.

Change the logic after handler invocation: if handler returns `NRPC_HANDLER_DEFERRED`, skip encoding and sending, skip setting `*is_send_pending = true` in the caller. Return 1 (dispatched) so the loop in `server_recv_callback` continues processing more frames.

```c
/* In try_dispatch_one, after calling handler: */
if (handler_rc == NRPC_HANDLER_DEFERRED) {
    /* Handler will send response later via nrpc_server_send_response.
     * Don't encode/send now. Don't set is_send_pending.
     * Consume the frame from accumulator and return 1. */
    server->accumulator_len -= (NRPC_HEADER_SIZE + body_length);
    if (server->accumulator_len > 0) {
        memmove(server->accumulator, server->accumulator + NRPC_HEADER_SIZE + body_length,
                server->accumulator_len);
    }
    free(response_buffer);
    return 1;
}
```

Add `nrpc_server_send_response` function:

```c
int
nrpc_server_send_response(struct nrpc_server *server, uint64_t request_id,
                           uint32_t status, const void *body, uint32_t body_len)
{
    spdk_assert(spdk_get_thread() == server->thread, "must be called from server thread");

    if (server->is_send_pending) {
        return -EBUSY;
    }

    uint8_t encoded[NRPC_MAX_BODY + NRPC_HEADER_SIZE];
    int encoded_len = nrpc_encode_response(encoded, sizeof(encoded), 1, 0,
                                           status, request_id, body, body_len);
    if (encoded_len < 0) {
        return -EINVAL;
    }

    /* Copy to heap for async send */
    uint8_t *send_buf = malloc(encoded_len);
    if (!send_buf) {
        return -ENOMEM;
    }
    memcpy(send_buf, encoded, encoded_len);

    struct server_send_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(send_buf);
        return -ENOMEM;
    }
    ctx->server = server;
    ctx->send_buf = send_buf;

    int rc = server->transport->ops->submit_send(server->conn, send_buf, encoded_len,
                                                  server_send_done, ctx);
    if (rc != 0) {
        free(ctx);
        free(send_buf);
        return rc;
    }

    server->is_send_pending = true;
    return 0;
}
```

- [ ] **Step 3: Build and verify nrpc tests still pass**

```bash
make -C src/rpc/test test_server_mock test_client_mock
cd src/rpc/test && ./test_server_mock && ./test_client_mock
```

- [ ] **Step 4: Commit**

```bash
git add src/rpc/src/server.h src/rpc/src/server.c
git commit -m "feat(rpc): add deferred reply support to nrpc_server"
```

---

### Task 2: Engine changes — expose shard accessors and RPC config

**Files:**
- Modify: `src/engine/src/obj.h`
- Modify: `src/engine/src/obj.c`
- Modify: `src/engine/src/storage_engine.h`
- Modify: `src/engine/src/storage_engine.c`

- [ ] **Step 1: Add shard accessor declarations to obj.h**

In `src/engine/src/obj.h`, add after existing declarations:

```c
/* Return number of shards in the shard manager */
int obj_manager_get_shard_count(obj_manager_t *m);

/* Return pointer to shard_manager (for cross-thread statfs queries) */
shard_manager_t *obj_manager_get_shard_manager(obj_manager_t *m);
```

- [ ] **Step 2: Implement accessors in obj.c**

In `src/engine/src/obj.c`, after `obj_manager_close`:

```c
int
obj_manager_get_shard_count(obj_manager_t *m)
{
    if (!m || !m->shards) {
        return 0;
    }
    return m->shards->num_shards;
}

shard_manager_t *
obj_manager_get_shard_manager(obj_manager_t *m)
{
    if (!m) {
        return NULL;
    }
    return m->shards;
}
```

- [ ] **Step 3: Add RPC fields to storage_engine_ctx_t**

In `src/engine/src/storage_engine.h`, add fields:

```c
typedef struct storage_engine_ctx {
    ...
    const char *config_file;
    int rpc_core_index;       /* CPU core for RPC server thread */
    int rpc_port;             /* TCP port for RPC server */
    spdk_event_fn ready_fn;
    ...
} storage_engine_ctx_t;
```

- [ ] **Step 4: Extend validate_core_affinity**

In `src/engine/src/storage_engine.c`, modify the validation to check that `rpc_core_index` is also in the reactor mask and doesn't conflict with shard cores or bg_core. Required cores: `num_shards + 1 (bg) + 1 (rpc)`.

```c
/* In validate_core_affinity, after existing checks, add: */
bool rpc_in_mask = spdk_cpuset_get_cpu(reactor_mask, ctx->rpc_core_index);
if (!rpc_in_mask) {
    SPDK_ERRLOG("RPC core %d not in reactor mask\n", ctx->rpc_core_index);
    return -1;
}
for (int i = 0; i < cfg->shard.num_shards; i++) {
    if (cfg->shard.core_map[i] == ctx->rpc_core_index) {
        SPDK_ERRLOG("RPC core %d conflicts with shard %d\n", ctx->rpc_core_index, i);
        return -1;
    }
}
if (cfg->shard.bg_core == ctx->rpc_core_index) {
    SPDK_ERRLOG("RPC core %d conflicts with bg core\n", ctx->rpc_core_index);
    return -1;
}
```

- [ ] **Step 5: Update required core count**

Change the minimum core check from `num_shards + 1` to `num_shards + 2`.

- [ ] **Step 6: Build engine to verify**

```bash
make -C src/engine
```

- [ ] **Step 7: Commit**

```bash
git add src/engine/src/obj.h src/engine/src/obj.c src/engine/src/storage_engine.h src/engine/src/storage_engine.c
git commit -m "feat(engine): expose shard accessors and add RPC core config"
```

---

### Task 3: Storage Client library

**Files:**
- Create: `include/lightfs/storage_client/storage_client.h`
- Create: `src/storage_client/storage_client_internal.h`
- Create: `src/storage_client/storage_client.c`
- Create: `src/storage_client/Makefile`

- [ ] **Step 1: Write public header**

Create `include/lightfs/storage_client/storage_client.h`:

```c
#ifndef LIGHTFS_STORAGE_CLIENT_H
#define LIGHTFS_STORAGE_CLIENT_H

#include <stdint.h>
#include "io_types.h"

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
```

- [ ] **Step 2: Write internal header**

Create `src/storage_client/storage_client_internal.h`:

```c
#ifndef STORAGE_CLIENT_INTERNAL_H
#define STORAGE_CLIENT_INTERNAL_H

#include "lightfs/storage_client/storage_client.h"
#include "lightfs/storage_server/storage_server.h"  /* for opcodes */
#include "rpc/client.h"
#include "spdk/thread.h"

#define STORAGE_CLIENT_DEFAULT_TIMEOUT_MS 5000

typedef enum {
    CLIENT_CALL_SIMPLE,
    CLIENT_CALL_READ,
    CLIENT_CALL_STATFS,
} client_call_type_t;

typedef struct {
    client_call_type_t        type;
    union {
        storage_client_callback       simple;
        storage_client_read_callback  read;
        storage_client_statfs_callback statfs;
    } callback;
    void *user_ctx;
    /* Pre-allocated output buffers for READ (filled from response body) */
    char     *read_buf;
    uint32_t  read_buf_len;
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
```

- [ ] **Step 3: Write storage_client.c — lifecycle functions**

Create `src/storage_client/storage_client.c`:

```c
#include "storage_client_internal.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* Forward */
static void client_ready_cb(void *ctx);
static void client_response_cb(void *ctx, uint64_t request_id, uint32_t status,
                                const void *body, uint32_t body_len, int transport_err);

storage_client_t *
storage_client_create(struct spdk_thread *thread, const char *host, uint16_t port)
{
    storage_client_t *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    c->thread = thread;
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
client_ready_cb(void *ctx)
{
    storage_client_t *c = ctx;
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
```

- [ ] **Step 4: Write storage_client.c — encode helpers**

In `src/storage_client/storage_client.c`, add encode functions:

```c
static int
encode_create_req(uint64_t oid, uint8_t *buf, uint32_t cap)
{
    if (cap < 8) return -1;
    memcpy(buf, &oid, 8);
    return 8;
}

static int
encode_delete_req(uint64_t oid, uint8_t *buf, uint32_t cap)
{
    if (cap < 8) return -1;
    memcpy(buf, &oid, 8);
    return 8;
}

static int
encode_write_req(uint64_t oid, uint64_t offset, const void *data,
                  uint32_t len, uint8_t *buf, uint32_t cap)
{
    uint32_t total = 8 + 8 + 4 + len;
    if (cap < total) return -1;
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
    if (cap < 20) return -1;
    memcpy(buf, &oid, 8);
    memcpy(buf + 8, &offset, 8);
    memcpy(buf + 16, &len, 4);
    return 20;
}

static int
encode_truncate_req(uint64_t oid, uint64_t size, uint8_t *buf, uint32_t cap)
{
    if (cap < 16) return -1;
    memcpy(buf, &oid, 8);
    memcpy(buf + 8, &size, 8);
    return 16;
}

static int
encode_punch_req(uint64_t oid, uint64_t offset, uint32_t len,
                  uint8_t *buf, uint32_t cap)
{
    if (cap < 20) return -1;
    memcpy(buf, &oid, 8);
    memcpy(buf + 8, &offset, 8);
    memcpy(buf + 16, &len, 4);
    return 20;
}

static int
encode_clone_req(uint64_t src_oid, uint64_t dst_oid, uint8_t *buf, uint32_t cap)
{
    if (cap < 16) return -1;
    memcpy(buf, &src_oid, 8);
    memcpy(buf + 8, &dst_oid, 8);
    return 16;
}
```

- [ ] **Step 5: Write storage_client.c — operation functions**

In `src/storage_client/storage_client.c`:

```c
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

    uint32_t max_data = 1024 * 1024 - 20;
    if (len > max_data) {
        free(pcall);
        return -EINVAL;
    }

    uint8_t body[1024 * 1024];
    int blen = encode_write_req(oid, offset, data, len, body, sizeof(body));
    return do_rpc_call(c, RPC_OP_WRITE, body, blen, pcall);
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
```

- [ ] **Step 6: Write storage_client.c — response callback**

```c
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
```

- [ ] **Step 7: Write Makefile**

Create `src/storage_client/Makefile`:

```makefile
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(abspath $(CURDIR)/../../include)
CFLAGS += -I$(abspath $(CURDIR)/../../src)
CFLAGS += -I$(abspath $(CURDIR)/../../src/rpc/src)
CFLAGS += -I$(abspath $(CURDIR)/../../src/engine/src)

LIB = libstorageclient.a
OBJS = storage_client.o

all: $(LIB)

$(LIB): $(OBJS)
	$(Q)ar rcs $@ $^

%.o: %.c storage_client_internal.h \
     $(abspath $(CURDIR)/../../include/lightfs/storage_client/storage_client.h) \
     $(abspath $(CURDIR)/../../include/lightfs/storage_server/storage_server.h)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB) $(OBJS)

.PHONY: all clean
```

- [ ] **Step 8: Build client library**

```bash
make -C src/storage_client
```

- [ ] **Step 9: Commit**

```bash
git add include/lightfs/storage_client/ src/storage_client/
git commit -m "feat(storage_client): add async RPC client library for storage server"
```

---

### Task 4: Storage Server — headers and lifecycle

**Files:**
- Create: `include/lightfs/storage_server/storage_server.h`
- Create: `src/storage_server/storage_server_internal.h`
- Create: `src/storage_server/storage_server.c`
- Create: `src/storage_server/Makefile`

- [ ] **Step 1: Write public header with opcodes and API**

Create `include/lightfs/storage_server/storage_server.h`:

```c
#ifndef LIGHTFS_STORAGE_SERVER_H
#define LIGHTFS_STORAGE_SERVER_H

#include <stdint.h>
#include "spdk/thread.h"

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

storage_server_t *storage_server_create(struct spdk_thread *rpc_thread,
                                         const char *host, uint16_t port,
                                         obj_manager_t *obj_mgr);
int  storage_server_start(storage_server_t *srv);
void storage_server_poll(storage_server_t *srv);
void storage_server_destroy(storage_server_t *srv);

#endif
```

- [ ] **Step 2: Write internal header**

Create `src/storage_server/storage_server_internal.h`:

```c
#ifndef STORAGE_SERVER_INTERNAL_H
#define STORAGE_SERVER_INTERNAL_H

#include "lightfs/storage_server/storage_server.h"
#include "rpc/server.h"
#include "rpc/transport.h"
#include "io_types.h"
#include "obj.h"
#include "shard.h"
#include <spdk/thread.h>

#define STORAGE_SERVER_MAX_PENDING 256

typedef struct {
    struct nrpc_server    *rpc_server;
    uint64_t               request_id;
    uint32_t               opcode;
    /* For READ: extra data from shard (data + length) */
    uint8_t               *extra_data;
    uint32_t               extra_len;
} bridge_ctx_t;

typedef struct {
    struct nrpc_server    *rpc_server;
    uint64_t               request_id;
    struct spdk_thread    *rpc_thread;
    io_result_t            result;
    uint32_t               opcode;
    /* Extra data for READ response body */
    uint8_t               *extra_data;
    uint32_t               extra_len;
} reply_msg_t;

struct storage_server {
    struct spdk_thread    *rpc_thread;
    struct nrpc_server    *nrpc;
    struct nrpc_transport *transport;
    obj_manager_t         *obj_mgr;
    char                  *host;
    uint16_t               port;
    bool                   started;
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
```

- [ ] **Step 3: Write storage_server.c — lifecycle**

Create `src/storage_server/storage_server.c`:

```c
#include "storage_server_internal.h"
#include <stdlib.h>
#include <string.h>

storage_server_t *
storage_server_create(struct spdk_thread *rpc_thread, const char *host,
                       uint16_t port, obj_manager_t *obj_mgr)
{
    storage_server_t *srv = calloc(1, sizeof(*srv));
    if (!srv) {
        return NULL;
    }
    srv->rpc_thread = rpc_thread;
    srv->obj_mgr = obj_mgr;
    srv->host = strdup(host);
    srv->port = port;
    srv->started = false;

    srv->transport = nrpc_tcp_transport_alloc();
    if (!srv->transport) {
        free(srv->host);
        free(srv);
        return NULL;
    }

    srv->nrpc = nrpc_server_create(rpc_thread, srv->transport);
    if (!srv->nrpc) {
        nrpc_tcp_transport_free(srv->transport);
        free(srv->host);
        free(srv);
        return NULL;
    }

    return srv;
}

int
storage_server_start(storage_server_t *srv)
{
    /* Register all 8 handlers */
    int registered = 0;
    registered += nrpc_server_register(srv->nrpc, RPC_OP_CREATE,    rpc_handler_create,    srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_DELETE,    rpc_handler_delete,    srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_WRITE,     rpc_handler_write,     srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_READ,      rpc_handler_read,      srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_TRUNCATE,  rpc_handler_truncate,  srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_PUNCH,     rpc_handler_punch,     srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_CLONE,     rpc_handler_clone,     srv);
    registered += nrpc_server_register(srv->nrpc, RPC_OP_STATFS,     rpc_handler_statfs,    srv);
    if (registered < 0) {
        return -1;
    }

    int rc = nrpc_server_listen(srv->nrpc, srv->host, srv->port);
    if (rc == 0) {
        srv->started = true;
    }
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
    nrpc_tcp_transport_free(srv->transport);
    free(srv->host);
    free(srv);
}
```

- [ ] **Step 4: Write Makefile**

Create `src/storage_server/Makefile`:

```makefile
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(abspath $(CURDIR)/../../include)
CFLAGS += -I$(abspath $(CURDIR)/../../src)
CFLAGS += -I$(abspath $(CURDIR)/../../src/rpc/src)
CFLAGS += -I$(abspath $(CURDIR)/../../src/engine/src)

LIB = libstorageserver.a
OBJS = storage_server.o rpc_handler.o callback_bridge.o

all: $(LIB)

$(LIB): $(OBJS)
	$(Q)ar rcs $@ $^

storage_server.o: storage_server.c storage_server_internal.h
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

rpc_handler.o: rpc_handler.c storage_server_internal.h
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

callback_bridge.o: callback_bridge.c storage_server_internal.h
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB) $(OBJS)

.PHONY: all clean
```

- [ ] **Step 5: Build to verify compilation**

```bash
make -C src/storage_server
```

- [ ] **Step 6: Commit**

```bash
git add include/lightfs/storage_server/ src/storage_server/
git commit -m "feat(storage_server): add server lifecycle and skeleton"
```

---

### Task 5: Storage Server — callback bridge

**Files:**
- Create: `src/storage_server/callback_bridge.c`

- [ ] **Step 1: Write callback_bridge.c**

Create `src/storage_server/callback_bridge.c`:

```c
#include "storage_server_internal.h"
#include "rpc/frame.h"
#include <stdlib.h>
#include <string.h>

void
send_deferred_reply(void *arg)
{
    reply_msg_t *msg = arg;

    /* Encode response body */
    uint8_t body[NRPC_MAX_BODY];
    uint32_t body_len = 0;

    switch (msg->opcode) {
    case RPC_OP_READ: {
        /* READ response: status(4) + data_len(4) + data */
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
        /* STATFS response: status(4) + total(8) + used(8) = 20 */
        /* The extra_data encodes total and used as two uint64_t */
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
        /* Simple response: just status(4) */
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
    msg->rpc_thread  = spdk_get_thread();  /* tmp, overwritten by caller if needed */
    msg->result      = result;
    msg->opcode      = bctx->opcode;
    msg->extra_data  = bctx->extra_data;
    msg->extra_len   = bctx->extra_len;

    /* Send msg to the RPC thread */
    spdk_thread_send_msg(msg->rpc_thread, send_deferred_reply, msg);

    free(bctx);
}
```

- [ ] **Step 2: Build**

```bash
make -C src/storage_server
```

- [ ] **Step 3: Commit**

```bash
git add src/storage_server/callback_bridge.c
git commit -m "feat(storage_server): add callback bridge for shard->RPC thread reply delivery"
```

---

### Task 6: Storage Server — RPC handlers for CREATE, DELETE, TRUNCATE, CLONE

**Files:**
- Create: `src/storage_server/rpc_handler.c`

- [ ] **Step 1: Write rpc_handler.c with CREATE, DELETE, TRUNCATE, CLONE handlers**

Create `src/storage_server/rpc_handler.c`:

```c
#include "storage_server_internal.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* --- Helper: allocate bridge context --- */
static bridge_ctx_t *
make_bridge_ctx(struct nrpc_server *rpc_server, uint64_t request_id,
                uint32_t opcode, struct spdk_thread *rpc_thread)
{
    bridge_ctx_t *bctx = calloc(1, sizeof(*bctx));
    bctx->rpc_server = rpc_server;
    bctx->request_id = request_id;
    bctx->opcode = opcode;
    return bctx;
}

/* --- CREATE (0x10): body = uint64_t object_id --- */
int
rpc_handler_create(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                   uint8_t *resp_buf, size_t resp_cap,
                   uint32_t *out_status, uint32_t *out_body_len)
{
    storage_server_t *srv = ctx;
    if (req_len < 8) {
        *out_status = NRPC_STATUS_BAD_OPCODE;
        *out_body_len = 0;
        return 0;
    }
    uint64_t oid;
    memcpy(&oid, req, 8);

    io_result_t rc = obj_create(srv->obj_mgr, oid, bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        /* Synchronous failure — encode response immediately */
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
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
    if (req_len < 8) {
        *out_status = NRPC_STATUS_BAD_OPCODE;
        *out_body_len = 0;
        return 0;
    }
    uint64_t oid;
    memcpy(&oid, req, 8);

    io_result_t rc = obj_delete(srv->obj_mgr, oid, bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
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
    if (req_len < 16) {
        *out_status = NRPC_STATUS_BAD_OPCODE;
        *out_body_len = 0;
        return 0;
    }
    uint64_t oid, size;
    memcpy(&oid, req, 8);
    memcpy(&size, (const uint8_t *)req + 8, 8);

    io_result_t rc = obj_truncate(srv->obj_mgr, oid, size, bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
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
    if (req_len < 16) {
        *out_status = NRPC_STATUS_BAD_OPCODE;
        *out_body_len = 0;
        return 0;
    }
    uint64_t src_oid, dst_oid;
    memcpy(&src_oid, req, 8);
    memcpy(&dst_oid, (const uint8_t *)req + 8, 8);

    io_result_t rc = obj_clone(srv->obj_mgr, src_oid, dst_oid, bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
        return 0;
    }

    return NRPC_HANDLER_DEFERRED;
}
```

- [ ] **Step 2: Build**

```bash
make -C src/storage_server
```

- [ ] **Step 3: Commit**

```bash
git add src/storage_server/rpc_handler.c
git commit -m "feat(storage_server): add CREATE, DELETE, TRUNCATE, CLONE RPC handlers"
```

---

### Task 7: Storage Server — RPC handlers for WRITE, READ, PUNCH

**Files:**
- Modify: `src/storage_server/rpc_handler.c`

- [ ] **Step 1: Add WRITE, READ, PUNCH handlers to rpc_handler.c**

Append to `src/storage_server/rpc_handler.c`:

```c
/* --- WRITE (0x12): body = uint64_t oid + uint64_t offset + uint32_t len + data[] --- */
int
rpc_handler_write(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                  uint8_t *resp_buf, size_t resp_cap,
                  uint32_t *out_status, uint32_t *out_body_len)
{
    storage_server_t *srv = ctx;
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

    if (len + 20 != req_len) {
        *out_status = NRPC_STATUS_BAD_OPCODE;
        *out_body_len = 0;
        return 0;
    }

    const void *data = (const uint8_t *)req + 20;

    io_result_t rc = obj_write(srv->obj_mgr, oid, offset, len, (char *)data,
                                bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
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

    /* Allocate buffer for read data; bridge will own it */
    char *read_buf = malloc(len);
    if (!read_buf) {
        *out_status = NRPC_STATUS_INTERNAL;
        *out_body_len = 0;
        return 0;
    }

    bridge_ctx_t *bctx = make_bridge_ctx(srv->nrpc,
                                          nrpc_server_get_request_id(srv->nrpc),
                                          RPC_OP_READ, srv->rpc_thread);
    bctx->extra_data = (uint8_t *)read_buf;
    bctx->extra_len = len;

    io_result_t rc = obj_read(srv->obj_mgr, oid, offset, len, read_buf,
                               bridge_callback, bctx);
    if (rc != IO_SUCCESS) {
        free(read_buf);
        free(bctx);
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
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

    io_result_t rc = obj_punch(srv->obj_mgr, oid, offset, len, bridge_callback, NULL);
    if (rc != IO_SUCCESS) {
        uint32_t st = rc;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
        return 0;
    }

    return NRPC_HANDLER_DEFERRED;
}
```

- [ ] **Step 2: Build**

```bash
make -C src/storage_server
```

- [ ] **Step 3: Commit**

```bash
git add src/storage_server/rpc_handler.c
git commit -m "feat(storage_server): add WRITE, READ, PUNCH RPC handlers"
```

---

### Task 8: Storage Server — STATFS handler with cross-shard aggregation

**Files:**
- Modify: `src/storage_server/rpc_handler.c`
- Modify: `src/storage_server/storage_server_internal.h`

- [ ] **Step 1: Add STATFS aggregator type to internal header**

In `src/storage_server/storage_server_internal.h`, add:

```c
typedef struct {
    struct nrpc_server    *rpc_server;
    uint64_t               request_id;
    struct spdk_thread    *rpc_thread;
    int                    remaining;
    uint64_t               total_size;
    uint64_t               used_size;
    uint32_t               error_count;
} statfs_aggregator_t;
```

- [ ] **Step 2: Add STATFS handler and aggregation logic to rpc_handler.c**

Append to `src/storage_server/rpc_handler.c`:

```c
/* Forward declaration for shard statfs callback */
static void
statfs_shard_callback(void *ctx)
{
    statfs_aggregator_t *agg = ctx;
    /* Called when a shard responds. Decrement remaining.
     * Last shard sends the final aggregated reply. */
    /* remaining and total/used are set by the caller before sending this msg */
}

/* Message handler: runs on a shard thread to collect io_statfs */
static void
collect_shard_statfs(void *arg)
{
    /* arg is a pointer to an aggregator; we add our shard's values */
    /* This is a placeholder — actual implementation uses the shard's io_manager */
}

/* --- STATFS (0x17): no body, aggregates across all shards --- */
int
rpc_handler_statfs(void *ctx, uint32_t opcode, const void *req, uint32_t req_len,
                   uint8_t *resp_buf, size_t resp_cap,
                   uint32_t *out_status, uint32_t *out_body_len)
{
    storage_server_t *srv = ctx;
    (void)req; (void)req_len;

    int num_shards = obj_manager_get_shard_count(srv->obj_mgr);
    if (num_shards == 0) {
        uint32_t st = IO_ERROR;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
        return 0;
    }

    statfs_aggregator_t *agg = calloc(1, sizeof(*agg));
    agg->rpc_server = srv->nrpc;
    agg->request_id = 0; /* FIXME: get from server dispatch ctx */
    agg->rpc_thread = srv->rpc_thread;
    agg->remaining = num_shards;

    shard_manager_t *shard_mgr = obj_manager_get_shard_manager(srv->obj_mgr);
    for (int i = 0; i < num_shards; i++) {
        shard_t *shard = &shard_mgr->shards[i];

        /* For each shard, send a message to collect statfs */
        /* We use a per-shard aggregation step: on the shard thread,
         * call io_statfs(io_mgr, &total, &used), then send results
         * back to the RPC thread for aggregation. */
        struct spdk_thread_send_msg(shard->thread, collect_shard_statfs, agg);
    }

    return NRPC_HANDLER_DEFERRED;
}
```

Wait — the STATFS aggregation needs more design work. The `io_statfs` operates on the shard's local `io_manager_t`. We need a way to call it from a message sent to the shard thread. The shard's `private_data` is a `shard_subsys_t` which contains `io_manager_t io`.

Strategy: The STATFS handler sends a message to each shard thread. The message handler accesses the shard's io_manager via `private_data`, calls `io_statfs`, then sends the results back to the RPC thread.

This requires:
- Access to `io_statfs()` from the storage_server (which is linked into the same binary)
- Access to shard `private_data` structure (`shard_subsys_t`)

Since the storage server is linked into the engine binary, it can include engine internal headers. Let me update the approach.

Actually, `shard_subsys_t` is private to `shard.c`. We need to either:
1. Move it to a shared internal header
2. Add a function that the message handler can call

Simpler: add a function `shard_get_statfs(shard_t *shard, uint64_t *total, uint64_t *used)` to `shard.c` / `shard.h` that internally accesses `private_data`.

Let me adjust the plan to include this.

- [ ] **Step 2 (revised): Add shard_get_statfs to engine**

In `src/engine/src/shard.h`, add:

```c
/* Collect per-shard filesystem stats. Called from the shard's own thread. */
int shard_get_statfs(shard_t *shard, uint64_t *total_size, uint64_t *used_size);
```

In `src/engine/src/shard.c`, add implementation:

```c
int
shard_get_statfs(shard_t *shard, uint64_t *total_size, uint64_t *used_size)
{
    if (!shard || !shard->private_data) {
        return -1;
    }
    shard_subsys_t *ss = shard->private_data;
    return io_statfs(&ss->io, total_size, used_size);
}
```

Now write the STATFS handler using this:

```c
typedef struct {
    statfs_aggregator_t *agg;
    uint64_t total;
    uint64_t used;
    int error;
} statfs_shard_result_t;

static void
collect_shard_statfs(void *arg)
{
    statfs_aggregator_t *agg = arg;
    shard_manager_t *shard_mgr = obj_manager_get_shard_manager(
        /* We need obj_mgr here — pass via aggregator */);
    /* Actually we need to identify which shard this msg was sent to.
     * Better: send one msg per shard with a per-shard context. */
}
```

OK, this is getting complex in plan form. Let me simplify the plan and just write the correct approach inline. The key insight: each `spdk_thread_send_msg` to a shard thread should carry a small context identifying the shard and aggregator.

Here's the corrected approach:

```c
typedef struct {
    statfs_aggregator_t *agg;
    shard_t *shard;
} statfs_shard_ctx_t;

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
        /* Last shard — send aggregated reply to RPC thread */
        reply_msg_t *msg = calloc(1, sizeof(*msg));
        msg->rpc_server = agg->rpc_server;
        msg->request_id = agg->request_id;
        msg->rpc_thread = agg->rpc_thread;
        msg->result = (agg->error_count > 0) ? IO_ERROR : IO_SUCCESS;
        msg->opcode = RPC_OP_STATFS;

        uint8_t *extra = malloc(16);
        memcpy(extra, &agg->total_size, 8);
        memcpy(extra + 8, &agg->used_size, 8);
        msg->extra_data = extra;
        msg->extra_len = 16;

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
    (void)req; (void)req_len;

    int num_shards = obj_manager_get_shard_count(srv->obj_mgr);
    if (num_shards == 0) {
        uint32_t st = IO_ERROR;
        memcpy(resp_buf, &st, 4);
        *out_status = NRPC_STATUS_OK;
        *out_body_len = 4;
        return 0;
    }

    shard_manager_t *shard_mgr = obj_manager_get_shard_manager(srv->obj_mgr);

    statfs_aggregator_t *agg = calloc(1, sizeof(*agg));
    agg->rpc_server = srv->nrpc;
    agg->request_id = nrpc_server_get_request_id(srv->nrpc);
    agg->rpc_thread = srv->rpc_thread;
    agg->remaining = num_shards;

    for (int i = 0; i < num_shards; i++) {
        statfs_shard_ctx_t *sctx = calloc(1, sizeof(*sctx));
        sctx->agg = agg;
        sctx->shard = &shard_mgr->shards[i];
        spdk_thread_send_msg(shard_mgr->shards[i].thread,
                              collect_shard_statfs, sctx);
    }

    return NRPC_HANDLER_DEFERRED;
}
```

But there's a problem: `collect_shard_statfs` runs on the shard thread and accesses `agg` atomically. But when `rem == 0`, it frees `agg` and sends the reply from the shard thread via `spdk_thread_send_msg`. The `send_deferred_reply` function needs to be called on the RPC thread (since nrpc_server_send_response asserts this). We're already using `spdk_thread_send_msg(agg->rpc_thread, send_deferred_reply, msg)` so that's fine.

But we need `__sync_fetch_and_add` and `__sync_sub_and_fetch` — these are GCC builtins, available. But there's a subtlety: the aggregator must stay alive until the last shard completes. We free it in the last callback. Good.

OK this is getting very detailed. Let me write the plan task with the correct code inline. I'll just present it as the corrected Step 2.

- [ ] **Step 2 (corrected): Add shard_get_statfs to engine + STATFS handler**

First, modify `src/engine/src/shard.h`:

```c
int shard_get_statfs(shard_t *shard, uint64_t *total_size, uint64_t *used_size);
```

Modify `src/engine/src/shard.c`:

```c
int
shard_get_statfs(shard_t *shard, uint64_t *total_size, uint64_t *used_size)
{
    if (!shard || !shard->private_data || !total_size || !used_size) {
        return -1;
    }
    shard_subsys_t *ss = shard->private_data;
    return io_statfs(&ss->io, total_size, used_size);
}
```

Then append the full STATFS handler (with aggregator struct, shard context, and collect/send logic) to `rpc_handler.c`.

- [ ] **Step 3: Build and verify**

```bash
make -C src/engine
make -C src/storage_server
```

- [ ] **Step 4: Commit**

```bash
git add src/engine/src/shard.h src/engine/src/shard.c \
        src/storage_server/rpc_handler.c src/storage_server/storage_server_internal.h
git commit -m "feat(storage_server): add STATFS handler with cross-shard aggregation"
```

---

### Task 9: Engine integration — create RPC thread at startup

**Files:**
- Modify: `src/engine/src/storage_engine.c`
- Modify: `src/engine/Makefile`

- [ ] **Step 1: Add RPC thread creation after obj_manager_init in storage_engine_start**

In `src/engine/src/storage_engine.c`, after `obj_manager_init` succeeds and before calling `ready_fn`, create the RPC thread and start the storage server:

```c
#include "lightfs/storage_server/storage_server.h"

/* After obj_manager_init in the mount path: */
if (ctx->rpc_port > 0) {
    /* Create RPC thread on dedicated core */
    char thread_name[32];
    snprintf(thread_name, sizeof(thread_name), "rpc_thread");
    struct spdk_cpuset *rpc_cpuset = spdk_cpuset_alloc();
    spdk_cpuset_zero(rpc_cpuset);
    spdk_cpuset_set_cpu(rpc_cpuset, ctx->rpc_core_index, true);
    struct spdk_thread *rpc_thread = spdk_thread_create(thread_name, rpc_cpuset);
    spdk_cpuset_free(rpc_cpuset);

    if (rpc_thread) {
        storage_server_t *srv = storage_server_create(rpc_thread, "0.0.0.0",
                                                       ctx->rpc_port, ctx->obj_manager);
        if (srv && storage_server_start(srv) == 0) {
            SPDK_NOTICELOG("Storage RPC server started on port %d, core %d\n",
                           ctx->rpc_port, ctx->rpc_core_index);
            /* Store srv for cleanup */
            ctx->storage_server = srv;
            ctx->rpc_thread = rpc_thread;
            /* Register poller on RPC thread */
            spdk_thread_send_msg(rpc_thread, register_rpc_poller, srv);
        }
    }
}
```

We need `storage_server_t *storage_server` and `struct spdk_thread *rpc_thread` in the ctx. Add to `storage_engine_ctx_t`.

And add `register_rpc_poller`:

```c
static void
register_rpc_poller(void *arg)
{
    storage_server_t *srv = arg;
    spdk_poller_register(storage_server_poller, srv, 0);
}

static int
storage_server_poller(void *arg)
{
    storage_server_t *srv = arg;
    storage_server_poll(srv);
    return SPDK_POLLER_BUSY;
}
```

- [ ] **Step 2: Update storage_engine_ctx_t with new fields**

In `src/engine/src/storage_engine.h`, add:

```c
struct storage_server;  /* forward */

typedef struct storage_engine_ctx {
    ...
    int rpc_core_index;
    int rpc_port;
    struct storage_server *storage_server;
    struct spdk_thread   *rpc_thread;
    ...
} storage_engine_ctx_t;
```

- [ ] **Step 3: Update engine Makefile to link storage_server and rpc**

In `src/engine/Makefile`, add include paths and link flags for `libstorageserver.a` and RPC libraries.

- [ ] **Step 4: Build**

```bash
make -C src/engine
```

- [ ] **Step 5: Commit**

```bash
git add src/engine/src/storage_engine.c src/engine/src/storage_engine.h src/engine/Makefile
git commit -m "feat(engine): integrate storage RPC server thread at startup"
```

---

### Task 10: Unit tests — RPC handler encode/decode

... (I'll continue with testing tasks)

Actually, I realize this plan is getting extremely long. Let me evaluate: the user is in auto mode and wants execution. I should write a reasonable plan but not take forever on it. Let me complete the remaining tasks more concisely.

Let me write the remaining tasks more briefly but still with concrete steps.<｜end▁of▁thinking｜>

<｜｜DSML｜｜tool_calls>
<｜｜DSML｜｜invoke name="Write">
<｜｜DSML｜｜parameter name="content" string="true"># Storage Server + Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Storage Server that wraps the engine behind RPC, plus a client library for remote storage access.

**Architecture:** RPC thread on a dedicated core, engine shard threads on their own cores. Server handlers call `obj_xxx()` async, bridge callbacks forward results back to RPC thread via `spdk_thread_send_msg`. Client library wraps `nrpc_client` with an async callback API mirroring the engine.

**Tech Stack:** C11, SPDK reactor model, nrpc binary RPC (TCP transport), engine obj_manager + shard

---

## File Map

### New files

| File | Responsibility |
|------|---------------|
| `include/lightfs/storage_server/storage_server.h` | Public server types, opcodes, API |
| `include/lightfs/storage_client/storage_client.h` | Public client types, callbacks, API |
| `src/storage_server/storage_server_internal.h` | Internal structs (server, bridge_ctx, reply_msg, statfs_aggregator) |
| `src/storage_server/storage_server.c` | Lifecycle: create, start, poll, destroy |
| `src/storage_server/rpc_handler.c` | 8 opcode handlers + STATFS cross-shard aggregation |
| `src/storage_server/callback_bridge.c` | Bridge callback + `send_deferred_reply` for shard→RPC thread |
| `src/storage_server/Makefile` | Build libstorageserver.a |
| `src/storage_client/storage_client_internal.h` | Internal structs (client, pending_call) |
| `src/storage_client/storage_client.c` | Encode, call, decode, callback dispatch |
| `src/storage_client/Makefile` | Build libstorageclient.a |

### Modified files

| File | Change |
|------|--------|
| `src/rpc/src/server.h` | Add `NRPC_HANDLER_DEFERRED`, `nrpc_server_send_response()` |
| `src/rpc/src/server.c` | Handle deferred return, add `nrpc_server_send_response()` |
| `src/engine/src/obj.h` | Add `obj_manager_get_shard_count()`, `obj_manager_get_shard_manager()` |
| `src/engine/src/obj.c` | Implement the two accessors |
| `src/engine/src/shard.h` | Add `shard_get_statfs()` |
| `src/engine/src/shard.c` | Implement `shard_get_statfs()` |
| `src/engine/src/storage_engine.h` | Add `rpc_core_index`, `rpc_port`, `storage_server`, `rpc_thread` fields |
| `src/engine/src/storage_engine.c` | Extend `validate_core_affinity`, create RPC thread + server after mount |
| `src/engine/Makefile` | Link libstorageserver.a, librpc.a, libtransport_tcp.a |

---

### Task 1: Add deferred reply support to nrpc_server

**Files:**
- Modify: `src/rpc/src/server.h`
- Modify: `src/rpc/src/server.c`

- [ ] **Step 1: Add `NRPC_HANDLER_DEFERRED`, `nrpc_server_send_response`, and request_id accessor to server.h**

In `src/rpc/src/server.h`, after the handler typedef:

```c
/* Handler returns this to defer response; send later via nrpc_server_send_response */
#define NRPC_HANDLER_DEFERRED 1

/**
 * Send a response for a previously deferred request.
 * Must be called from the server's SPDK thread.
 */
int nrpc_server_send_response(struct nrpc_server *server, uint64_t request_id,
                               uint32_t status, const void *body, uint32_t body_len);

/** Get the request_id of the currently dispatching request. For deferred handlers. */
uint64_t nrpc_server_get_request_id(struct nrpc_server *server);
```

- [ ] **Step 2: Implement deferred dispatch, send_response, and request_id tracking in server.c**

In `try_dispatch_one()`: set `server->current_request_id = header.request_id` before calling the handler. If handler returns `NRPC_HANDLER_DEFERRED`, skip encoding/sending, free the response buffer, consume the frame from the accumulator, return 1 without setting `is_send_pending`.

Add `nrpc_server_send_response()`: encodes response frame, allocates send buffer, calls `submit_send`, sets `is_send_pending = true`.

Add `nrpc_server_get_request_id()`: returns `server->current_request_id`. Add `uint64_t current_request_id` field to `struct nrpc_server`.

- [ ] **Step 3: Verify existing RPC tests pass**

```bash
make -C src/rpc/test test_server_mock test_client_mock
cd src/rpc/test && ./test_server_mock && ./test_client_mock
```

- [ ] **Step 4: Commit**

```bash
git add src/rpc/src/server.h src/rpc/src/server.c
git commit -m "feat(rpc): add deferred reply support to nrpc_server"
```

---

### Task 2: Engine changes — expose shard accessors, statfs, and RPC config

**Files:**
- Modify: `src/engine/src/obj.h`, `src/engine/src/obj.c`
- Modify: `src/engine/src/shard.h`, `src/engine/src/shard.c`
- Modify: `src/engine/src/storage_engine.h`, `src/engine/src/storage_engine.c`

- [ ] **Step 1: Add obj_manager accessors**

In `src/engine/src/obj.h`, add:
```c
int obj_manager_get_shard_count(obj_manager_t *m);
shard_manager_t *obj_manager_get_shard_manager(obj_manager_t *m);
```

In `src/engine/src/obj.c`, implement them:
```c
int obj_manager_get_shard_count(obj_manager_t *m) {
    return (m && m->shards) ? m->shards->num_shards : 0;
}
shard_manager_t *obj_manager_get_shard_manager(obj_manager_t *m) {
    return m ? m->shards : NULL;
}
```

- [ ] **Step 2: Add shard_get_statfs**

In `src/engine/src/shard.h`, add:
```c
int shard_get_statfs(shard_t *shard, uint64_t *total_size, uint64_t *used_size);
```

In `src/engine/src/shard.c`, implement — accesses `shard->private_data` (shard_subsys_t), calls `io_statfs(&ss->io, total, used)`.

- [ ] **Step 3: Add RPC config fields to storage_engine_ctx_t**

In `src/engine/src/storage_engine.h`, add fields:
```c
int rpc_core_index;
int rpc_port;
struct storage_server *storage_server;
struct spdk_thread   *rpc_thread;
```

- [ ] **Step 4: Extend validate_core_affinity**

Add validation that `rpc_core_index` is in reactor mask and doesn't conflict with shard cores or bg_core. Update minimum core check from `num_shards + 1` to `num_shards + 2`.

- [ ] **Step 5: Build engine**

```bash
make -C src/engine
```

- [ ] **Step 6: Commit**

```bash
git add src/engine/src/obj.h src/engine/src/obj.c \
        src/engine/src/shard.h src/engine/src/shard.c \
        src/engine/src/storage_engine.h src/engine/src/storage_engine.c
git commit -m "feat(engine): expose shard accessors, statfs, and RPC core config"
```

---

### Task 3: Storage Client library

**Files:**
- Create: `include/lightfs/storage_client/storage_client.h`
- Create: `src/storage_client/storage_client_internal.h`
- Create: `src/storage_client/storage_client.c`
- Create: `src/storage_client/Makefile`

- [ ] **Step 1: Write public header**

Create `include/lightfs/storage_client/storage_client.h` with callback typedefs (`storage_client_callback`, `storage_client_read_callback`, `storage_client_statfs_callback`), opaque `storage_client_t`, and 8 operation functions + `create/destroy/poll`.

- [ ] **Step 2: Write internal header**

Create `src/storage_client/storage_client_internal.h` with `storage_pending_call_t` (tagged union for SIMPLE/READ/STATFS callback types) and `storage_client_t` struct (thread, nrpc_client, endpoint_idx, is_connected, host, port).

- [ ] **Step 3: Write storage_client.c — lifecycle**

Implement `storage_client_create` (alloc, create nrpc_client, add TCP endpoint with `client_ready_cb`), `storage_client_destroy`, `storage_client_poll` (forwards to `nrpc_client_poll`).

- [ ] **Step 4: Write storage_client.c — encode helpers**

Add static encode functions for each opcode: `encode_create_req`, `encode_delete_req`, `encode_write_req` (oid+offset+len+data), `encode_read_req`, `encode_truncate_req`, `encode_punch_req`, `encode_clone_req`. All write little-endian structs to byte buffer.

- [ ] **Step 5: Write storage_client.c — operation functions**

Implement all 8 `storage_client_xxx()` functions. Each allocates a `storage_pending_call_t`, encodes the request body, calls `nrpc_client_call` with `NRPC_IFLAG_IDEMPOTENT` and `client_response_cb`.

- [ ] **Step 6: Write storage_client.c — response callback**

Implement `client_response_cb`: on transport error → call user callback with `IO_ERROR`. On success, decode 4-byte status from body. For READ: additionally extract `data_len` and `data[]`. For STATFS: additionally extract `total` and `used`. Free `pcall`.

- [ ] **Step 7: Write Makefile and build**

```bash
make -C src/storage_client
```

- [ ] **Step 8: Commit**

```bash
git add include/lightfs/storage_client/ src/storage_client/
git commit -m "feat(storage_client): add async RPC client library for storage server"
```

---

### Task 4: Storage Server — headers, lifecycle, callback bridge

**Files:**
- Create: `include/lightfs/storage_server/storage_server.h`
- Create: `src/storage_server/storage_server_internal.h`
- Create: `src/storage_server/storage_server.c`
- Create: `src/storage_server/callback_bridge.c`
- Create: `src/storage_server/Makefile`

- [ ] **Step 1: Write public header**

Create `include/lightfs/storage_server/storage_server.h` with 8 opcode defines (`RPC_OP_CREATE` 0x10 through `RPC_OP_STATFS` 0x17), opaque `storage_server_t`, and lifecycle API: `storage_server_create(thread, host, port, obj_mgr)`, `_start`, `_poll`, `_destroy`.

- [ ] **Step 2: Write internal header**

Create `src/storage_server/storage_server_internal.h` with:
- `bridge_ctx_t` — holds rpc_server, request_id, opcode, extra_data/len (for READ/STATFS output)
- `reply_msg_t` — holds rpc_server, request_id, rpc_thread, result, opcode, extra_data/len
- `statfs_aggregator_t` — holds rpc_server, request_id, rpc_thread, remaining, total_size, used_size, error_count (atomic fields)
- `statfs_shard_ctx_t` — holds aggregator pointer + shard pointer
- `storage_server_t` struct — holds rpc_thread, nrpc, transport, obj_mgr, host, port, started

Forward declare all 8 handler functions and `send_deferred_reply`/`bridge_callback`.

- [ ] **Step 3: Write storage_server.c**

Implement lifecycle:
- `storage_server_create`: alloc struct, create TCP transport, create nrpc_server
- `storage_server_start`: register all 8 handlers, call `nrpc_server_listen`
- `storage_server_poll`: forward to `nrpc_server_poll`
- `storage_server_destroy`: destroy nrpc_server, free transport, free host, free self

- [ ] **Step 4: Write callback_bridge.c**

Implement `send_deferred_reply`: runs on RPC thread, encodes response body based on opcode (READ → status+data_len+data, STATFS → status+total+used, others → status), calls `nrpc_server_send_response`, frees msg.

Implement `bridge_callback`: runs on shard thread, allocates `reply_msg_t`, copies result + extra_data, sends `spdk_thread_send_msg(rpc_thread, send_deferred_reply, msg)`, frees bridge_ctx.

- [ ] **Step 5: Build**

```bash
make -C src/storage_server
```

- [ ] **Step 6: Commit**

```bash
git add include/lightfs/storage_server/ src/storage_server/
git commit -m "feat(storage_server): add server lifecycle and callback bridge"
```

---

### Task 5: Storage Server — RPC handlers (all 8 operations)

**Files:**
- Create: `src/storage_server/rpc_handler.c`

- [ ] **Step 1: Write handler helper and simple handlers (CREATE, DELETE, TRUNCATE, CLONE)**

In `rpc_handler.c`, add `make_bridge_ctx()` helper. Implement 4 handlers:

- **CREATE (0x10):** decode `uint64_t oid` from 8-byte body, call `obj_create(obj_mgr, oid, bridge_callback, NULL)`. On sync error → encode 4-byte status and return 0. On async success → return `NRPC_HANDLER_DEFERRED`.
- **DELETE (0x11):** same pattern, call `obj_delete`.
- **TRUNCATE (0x14):** decode `oid(8) + size(8)` from 16-byte body, call `obj_truncate`.
- **CLONE (0x16):** decode `src_oid(8) + dst_oid(8)` from 16-byte body, call `obj_clone`.

- [ ] **Step 2: Add data handlers (WRITE, READ, PUNCH)**

- **WRITE (0x12):** decode `oid(8) + offset(8) + len(4)`, validate `len + 20 == req_len`, extract `data` at offset 20, call `obj_write(obj_mgr, oid, offset, len, data, bridge_callback, NULL)`.
- **READ (0x13):** decode `oid(8) + offset(8) + len(4)`, malloc read buffer, create `bridge_ctx_t` with `extra_data=read_buf, extra_len=len`, call `obj_read(obj_mgr, oid, offset, len, read_buf, bridge_callback, bctx)`.
- **PUNCH (0x15):** decode `oid(8) + offset(8) + len(4)`, call `obj_punch`.

- [ ] **Step 3: Add STATFS handler with cross-shard aggregation**

- **collect_shard_statfs:** runs on shard thread via `spdk_thread_send_msg`. Calls `shard_get_statfs(shard, &total, &used)`. Uses `__sync_fetch_and_add` to update aggregator totals. Decrements `remaining`. When `remaining == 0` (last shard), allocates reply_msg with encoded total+used, sends `spdk_thread_send_msg(rpc_thread, send_deferred_reply, msg)`, frees aggregator.
- **rpc_handler_statfs:** gets `num_shards` via `obj_manager_get_shard_count`, creates `statfs_aggregator_t`, for each shard creates `statfs_shard_ctx_t`, sends `spdk_thread_send_msg(shard->thread, collect_shard_statfs, sctx)`. Returns `NRPC_HANDLER_DEFERRED`.

- [ ] **Step 4: Build**

```bash
make -C src/storage_server
```

- [ ] **Step 5: Commit**

```bash
git add src/storage_server/rpc_handler.c
git commit -m "feat(storage_server): add all 8 RPC handlers including STATFS aggregation"
```

---

### Task 6: Engine integration — create RPC thread at startup

**Files:**
- Modify: `src/engine/src/storage_engine.c`
- Modify: `src/engine/src/storage_engine.h`
- Modify: `src/engine/Makefile`

- [ ] **Step 1: Add RPC poller callback**

In `storage_engine.c`, add:
```c
static int storage_server_poller(void *arg) {
    storage_server_poll((storage_server_t *)arg);
    return SPDK_POLLER_BUSY;
}
```

- [ ] **Step 2: Create RPC thread after obj_manager_init in mount path**

In the mount path of `storage_engine_start`, after `obj_manager_init` succeeds, if `ctx->rpc_port > 0`:
1. Create RPC thread via `spdk_thread_create("rpc_thread", rpc_cpuset)` with core `ctx->rpc_core_index`
2. Call `storage_server_create(rpc_thread, "0.0.0.0", ctx->rpc_port, ctx->obj_manager)`
3. Call `storage_server_start(srv)`
4. Send `spdk_thread_send_msg(rpc_thread, register_poller, srv)` where `register_poller` calls `spdk_poller_register(storage_server_poller, srv, 0)`
5. Store `srv` and `rpc_thread` in ctx for cleanup

- [ ] **Step 3: Update shutdown path**

In `storage_engine_stop` and `storage_engine_stop_async`, add cleanup: destroy storage_server, send `spdk_thread_exit` to rpc_thread.

- [ ] **Step 4: Update engine Makefile**

Add `-I$(CURDIR)/../storage_server` to CFLAGS. Add `libstorageserver.a`, `librpc.a`, `libtransport_tcp.a` to link. (Or add source-level includes per SPDK build convention.)

- [ ] **Step 5: Build engine**

```bash
make -C src/engine
```

- [ ] **Step 6: Commit**

```bash
git add src/engine/ src/storage_server/
git commit -m "feat(engine): integrate RPC server thread at startup"
```

---

### Task 7: Unit tests — RPC protocol encode/decode (mock transport)

**Files:**
- Create: `test/storage_server/test_rpc_handler.c`
- Create: `test/storage_server/Makefile`

- [ ] **Step 1: Write test_rpc_handler.c**

Use `nrpc_mock_transport_pair` + in-process `obj_manager_t` (from engine). Test each handler:

1. **CREATE roundtrip:** encode create request → handler decodes → calls obj_create → verify success
2. **DELETE roundtrip:** same pattern
3. **WRITE + READ roundtrip:** write data → read back → verify data matches
4. **TRUNCATE:** create + write + truncate → verify reduced size
5. **PUNCH:** create + write two ranges + punch middle → verify
6. **CLONE:** create src + write + clone to dst → verify dst data
7. **STATFS:** query → verify total > 0

Entry point: `spdk_app_start(&opts, app_start_fn, NULL)` with mock thread, `no_huge=true`, `mem_size=1024`.

- [ ] **Step 2: Write Makefile**

SPDK app build pattern:
```makefile
SPDK_ROOT_DIR = /root/spdk
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
APP = test_rpc_handler
C_SRCS := test_rpc_handler.c \
          ../../src/storage_server/storage_server.c \
          ../../src/storage_server/rpc_handler.c \
          ../../src/storage_server/callback_bridge.c \
          ../../src/rpc/src/server.c ../../src/rpc/src/client.c \
          ../../src/rpc/src/frame.c ../../src/rpc/src/transport_mock.c \
          ../../src/engine/src/obj.c ../../src/engine/src/shard.c
# Note: shard.c needs SPDK; provide stubs for shard_manager_init, etc. or use a
# minimal engine init that creates an in-memory obj_manager without real shard threads.
SPDK_LIB_LIST = event thread env_dpdk util log
include $(SPDK_ROOT_DIR)/mk/spdk.app.mk
```

- [ ] **Step 3: Build and run**

```bash
cd test/storage_server && make && ./test_rpc_handler
```

Expected: all tests PASS, exit 0.

- [ ] **Step 4: Commit**

```bash
git add test/storage_server/
git commit -m "test(storage_server): add unit tests for RPC handler encode/decode"
```

---

### Task 8: Unit tests — Client library (mock transport)

**Files:**
- Create: `test/storage_client/test_client.c`
- Create: `test/storage_client/Makefile`

- [ ] **Step 1: Write test_client.c**

Use mock transport pair. Create server + client on same thread. Test each client API function:

1. `storage_client_create_obj` → verify callback receives `IO_SUCCESS`
2. `storage_client_write` + `storage_client_read` → verify data roundtrip
3. `storage_client_delete_obj` → verify `IO_NOT_FOUND` on subsequent read
4. `storage_client_clone` → verify cloned data
5. `storage_client_statfs` → verify callback receives total/used

- [ ] **Step 2: Write Makefile and run**

```bash
cd test/storage_client && make && ./test_client
```

- [ ] **Step 3: Commit**

```bash
git add test/storage_client/
git commit -m "test(storage_client): add unit tests for client encode/decode"
```

---

### Task 9: Integration tests — TCP transport + real engine (basic operations)

**Files:**
- Create: `test/storage_server/test_integration.c`
- Create: `test/storage_server/Makefile.integration`

- [ ] **Step 1: Write integration test**

SPDK app with two threads: RPC server thread + 1 shard thread. Use real engine (malloc bdev, 1 shard, 1 rpc core). TCP transport on loopback.

Test cases:

| Test | Steps | Expected |
|------|-------|----------|
| CREATE | `storage_client_create_obj(oid)` | callback: `IO_SUCCESS` |
| CREATE duplicate | create same oid twice | second callback: `IO_ERROR` |
| WRITE + READ | write 4096B at off 0, read same | READ callback: `IO_SUCCESS`, data matches, len=4096 |
| READ non-existent | read never-created oid | callback: `IO_NOT_FOUND` |
| DELETE | create then delete | DELETE callback: `IO_SUCCESS` |
| DELETE non-existent | delete never-created oid | callback: `IO_NOT_FOUND` |
| TRUNCATE | create + write 4096 + truncate to 1024 + read 4096 | READ returns `IO_SUCCESS`, len=1024 |
| PUNCH | create + write [0,4096] + punch [1024,2048] + read [1024,2048] | punched range returns zero data |
| CLONE | create src + write data + clone to dst + read dst | READ dst: `IO_SUCCESS`, data matches src |
| STATFS | query after CREATE+WRITE | callback: `IO_SUCCESS`, total > 0 |

Entry point: `spdk_app_start()` with multi-core config from JSON file. Config specifies 1 shard core + 1 RPC core (cores 0 and 1 within reactor mask).

- [ ] **Step 2: Create test config.json**

```json
{
  "shard": {
    "num_shards": 1,
    "core_map": [1],
    "bg_core": 2
  },
  "bdev": {
    "name": "Malloc0",
    "size_mb": 256
  },
  "rpc": {
    "core_index": 0,
    "port": 9999
  }
}
```

- [ ] **Step 3: Write Makefile and run**

```bash
cd test/storage_server && make -f Makefile.integration && sudo ./test_integration --no-pci
```

- [ ] **Step 4: Commit**

```bash
git add test/storage_server/test_integration.c test/storage_server/Makefile.integration
git commit -m "test(storage_server): add TCP integration tests covering all 8 operations"
```

---

### Task 10: Integration tests — Large-scale IO (100,000 operations)

**Files:**
- Create: `test/storage_server/test_bench.c`
- Create: `test/storage_server/Makefile.bench`

- [ ] **Step 1: Write large-scale IO test**

SPDK app with real engine, TCP loopback. Config: 1 shard + 1 RPC core.

Test flow:
```c
#define IO_COUNT 100000
#define OBJ_COUNT 1000

static int completed = 0;

static void bench_callback(void *ctx, io_result_t result) {
    assert(result == IO_SUCCESS);
    completed++;
}
```

1. Create 1000 objects — wait for all callbacks
2. Issue 100k random writes across those objects (random oid, random offset <= 4KB, random 64-4096 byte data) — wait for all callbacks
3. Issue 100k random reads — verify data integrity per read (pre-computed expected data for each oid+offset)
4. Verify `completed == IO_COUNT * 2 + OBJ_COUNT` (creates + writes + reads)
5. Log elapsed time and IOPS

- [ ] **Step 2: Handle inflight limits**

Client inflight limit is 64 per endpoint. Limit concurrent inflight to 64 by only submitting new requests from within the callback (when `completed < target`).

- [ ] **Step 3: Write Makefile and run**

```bash
cd test/storage_server && make -f Makefile.bench && sudo ./test_bench --no-pci
```

Expected: all 200k+ operations complete successfully, no hangs, no lost callbacks, IOPS reported.

- [ ] **Step 4: Commit**

```bash
git add test/storage_server/test_bench.c test/storage_server/Makefile.bench
git commit -m "test(storage_server): add 100k IO large-scale benchmark test"
```

---

### Task 11: Top-level build integration and final polish

- [ ] **Step 1: Add storage_server and storage_client to top-level `make test`**

Update root `Makefile` to build `src/storage_server`, `src/storage_client`, and their tests. Add `make -C test/storage_server` and `make -C test/storage_client` to `make test` target (for unit tests only; integration tests require sudo/manual run).

- [ ] **Step 2: Clean up any compilation warnings**

```bash
make -C src/storage_server clean && make -C src/storage_server
make -C src/storage_client clean && make -C src/storage_client
```

Fix any warnings.

- [ ] **Step 3: Final commit**

```bash
git add Makefile
git commit -m "build: integrate storage_server and storage_client into top-level build"
```
