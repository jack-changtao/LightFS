# LightFS Storage Server Design Spec

**Date:** 2026-05-19
**Status:** Draft

## Motivation

The LightFS storage engine (`src/engine/`) is fully implemented with 8 object operations (create, delete, write, read, truncate, punch, clone, statfs), each shard running on a dedicated SPDK thread. However, the engine has no network interface — it can only be called via in-process function calls. Gateway and other modules need to access storage over the network.

This spec defines a Storage Server that wraps the engine behind an RPC interface, plus a client library for modules that need to call it remotely.

## Architecture

### Process & Thread Model

```
                   ┌──────────────────────────────────┐
                   │        SPDK App Process          │
                   │                                  │
 TCP/RDMA ─────── │ Core R: [RPC Server Thread]      │
  Client          │   nrpc_server poll               │
  (Gateway)       │   handler dispatch               │
                   │   callback bridge               │
                   │                                  │
                   │ Core 1: [Shard 0 Thread]         │
                   │   WAL | Manifest | IO | GC       │
                   │                                  │
                   │ Core 2: [Shard 1 Thread]         │
                   │   WAL | Manifest | IO | GC       │
                   │                                  │
                   │ Core N: [Shard N-1 Thread]       │
                   └──────────────────────────────────┘
```

- RPC server thread runs on its own dedicated CPU core (configurable via `rpc.core_index`)
- Each engine shard thread runs on its own core (existing `shard.core_map[]`)
- RPC core, shard cores, and bg_core are all disjoint (validated at startup)
- Core validation: `num_shards + 1 (bg) + 1 (rpc)` cores required in reactor mask

### Thread Hop Path (per operation)

```
RPC Thread                     Shard Thread
──────────                     ───────────
1. receive request frame
2. decode → opcode + body
3. call obj_xxx(obj_mgr, ...) ──spdk_thread_send_msg──→ 4. enqueue ring_buf[1024]
                                                        5. shard_req_handler pop
                                                        6. execute io_xxx
                                                        7. callback fires
8. bridge cb receives result ←──spdk_thread_send_msg──
9. encode response
10. submit_send reply
```

Each operation involves 2 thread hops. Data buffers (write payload / read result) are carried inside `obj_request_t` structs between hops; the engine already handles the memcpy internally.

### Module Placement

```
src/storage_server/              src/storage_client/
  storage_server.h                 storage_client.h
  storage_server.c                 storage_client.c
  rpc_handler.c
  callback_bridge.c
  callback_bridge.h

include/lightfs/storage_server/   include/lightfs/storage_client/
  storage_server.h                   storage_client.h
```

Dependencies:
```
Gateway ── libstorageclient.a ── librpc.a (nrpc_client) ── libtransport_tcp.a

Engine  ── libstorageserver.a ─┬─ libengine.a (obj_manager, shard)
                                └─ librpc.a (nrpc_server) ── libtransport_tcp.a
```

## RPC Protocol

Binary structs, little-endian, carried in RPC frame body. Max body size: 1 MiB (`NRPC_MAX_BODY`).

### Opcodes

| Opcode | Value | Operation |
|--------|-------|-----------|
| `RPC_OP_CREATE` | 0x10 | Create object |
| `RPC_OP_DELETE` | 0x11 | Delete object |
| `RPC_OP_WRITE` | 0x12 | Write data at offset |
| `RPC_OP_READ` | 0x13 | Read data from offset |
| `RPC_OP_TRUNCATE` | 0x14 | Truncate object |
| `RPC_OP_PUNCH` | 0x15 | Hole punch / deallocate range |
| `RPC_OP_CLONE` | 0x16 | Clone object (src → dst) |
| `RPC_OP_STATFS` | 0x17 | Filesystem statistics |

### Request Body Formats

```c
// CREATE / DELETE — just the object_id
// [uint64_t object_id]  (8 bytes)

// WRITE
//   uint64_t object_id   (8)
//   uint64_t offset      (8)
//   uint32_t length      (4)
//   uint8_t  data[]      (length bytes)

// READ
//   uint64_t object_id   (8)
//   uint64_t offset      (8)
//   uint32_t length      (4)

// TRUNCATE
//   uint64_t object_id   (8)
//   uint64_t size        (8)

// PUNCH
//   uint64_t object_id   (8)
//   uint64_t offset      (8)
//   uint32_t length      (4)

// CLONE
//   uint64_t src_object_id  (8)
//   uint64_t dst_object_id  (8)

// STATFS — empty body (0 bytes)
```

### Response Body Formats

```c
// CREATE / DELETE / WRITE / TRUNCATE / PUNCH / CLONE response
//   uint32_t status   (4 bytes, io_result_t: 0=SUCCESS, 1=ERROR, 2=NOT_FOUND, ...)

// READ response
//   uint32_t status   (4)
//   uint32_t length   (4, bytes actually read)
//   uint8_t  data[]   (length bytes)

// STATFS response
//   uint32_t status      (4)
//   uint64_t total_size  (8)
//   uint64_t used_size   (8)
```

## Server Design

### Lifecycle API

```c
storage_server_t *storage_server_create(struct spdk_thread *rpc_thread,
                                         struct nrpc_transport *transport,
                                         obj_manager_t *obj_mgr);

int storage_server_start(storage_server_t *srv, const char *host, uint16_t port);
void storage_server_poll(storage_server_t *srv);
void storage_server_destroy(storage_server_t *srv);
```

- `storage_server_create` — allocates server struct, creates underlying `nrpc_server`
- `storage_server_start` — registers all 8 handlers via `nrpc_server_register`, starts listening
- `storage_server_poll` — drives `nrpc_server_poll` plus drains pending reply queue (see below)
- `storage_server_destroy` — tears down server and frees resources

### Handler Flow

All 8 handlers follow the same pattern:

1. Decode request body into the appropriate struct
2. Validate body length against expected struct size
3. Call the corresponding `obj_xxx(obj_mgr, ...)` with a bridge callback
4. Return immediately (no response sent yet)

The bridge callback delivers the engine result to a **pending reply queue**, which is drained on the RPC thread during `storage_server_poll`.

### Callback Bridge

Because engine callbacks fire on the shard thread but RPC responses must be sent from the RPC thread, we need a cross-thread reply mechanism:

```c
struct pending_reply {
  struct nrpc_connection *conn;
  uint64_t request_id;
  uint8_t *encoded_frame;
  uint32_t frame_len;
  STAILQ_ENTRY(pending_reply) link;
};
```

- Bridge callback (on shard thread): encodes the full RPC response frame (header + status + optional data), pushes it into the server's `pending_reply` STAILQ, then sends `spdk_thread_send_msg` to wake the RPC thread
- `storage_server_poll` (on RPC thread): drains the STAILQ, calls `submit_send` for each pending reply

### STATFS Aggregation

STATFS queries all shards and aggregates results. The handler:

1. Initializes an aggregator: `{remaining = num_shards, total = 0, used = 0}`
2. For each shard, sends `spdk_thread_send_msg` to the shard thread to collect its local statfs
3. Each shard response updates the aggregator under a spinlock or atomic operations
4. The last shard to respond (when `remaining` reaches 0 after decrement) encodes the final STATFS response and pushes it to the pending reply queue

Requires engine change: expose `obj_manager_get_shard_count()` and per-shard statfs interface.

### Error Handling (Server)

| Scenario | Behavior |
|----------|----------|
| Body too short for expected struct | Set `status = NRPC_STATUS_BAD_OPCODE`, send immediately |
| Body `length` field > `NRPC_MAX_BODY` | Set `status = NRPC_STATUS_PAYLOAD_TOO_LARGE`, send immediately |
| `obj_xxx` returns synchronous error (e.g., `IO_INVALID_PARAM`) | Encode error response immediately, skip thread hop |
| Unknown opcode | Handled by `nrpc_server` framework: `NRPC_STATUS_BAD_OPCODE` |

## Client Library

### API

All operations are async with callback. Simple operations use a common type; READ and STATFS have operation-specific callbacks that include output data:

```c
typedef void (*storage_client_callback)(void *ctx, io_result_t result);
typedef void (*storage_client_read_callback)(void *ctx, io_result_t result,
                                              const void *data, uint32_t len);
typedef void (*storage_client_statfs_callback)(void *ctx, io_result_t result,
                                                uint64_t total, uint64_t used);
```

Lifecycle and operations:

```c
storage_client_t *storage_client_create(struct spdk_thread *thread,
                                         const char *host, uint16_t port);
void storage_client_destroy(storage_client_t *client);
void storage_client_poll(storage_client_t *client);

int storage_client_create(storage_client_t *c, uint64_t oid,
                          storage_client_callback cb, void *ctx);
int storage_client_delete(storage_client_t *c, uint64_t oid,
                          storage_client_callback cb, void *ctx);
int storage_client_write(storage_client_t *c, uint64_t oid,
                          uint64_t offset, const void *data, uint32_t len,
                          storage_client_callback cb, void *ctx);
int storage_client_read(storage_client_t *c, uint64_t oid,
                         uint64_t offset, uint32_t len, char *buf,
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
```

### Internal Implementation

```c
struct storage_client {
  struct spdk_thread *thread;
  struct nrpc_client *rpc_client;
  int                 endpoint_idx;
};
```

- `storage_client_create` wraps `nrpc_client_create` + `nrpc_client_add_endpoint` with TCP transport
- Each operation encodes its request struct, calls `nrpc_client_call` with the corresponding opcode
- Internal `response_cb` decodes the response body, maps `nrpc_status` + body → `io_result_t` + output data, then invokes the user's callback
- `storage_client_poll` calls `nrpc_client_poll` to drive I/O, deliver response callbacks, and handle reconnects

### Idempotency & Reconnection

All operations use `NRPC_IFLAG_IDEMPOTENT`. On connection loss, the RPC client automatically replays all inflight requests after reconnect. Duplicate execution side-effects are the responsibility of the business layer (Gateway).

### Error Mapping

| Transport | App-Level |
|-----------|-----------|
| `transport_err != 0` (-ENOTCONN, -ECONNRESET, -ETIMEDOUT) | `IO_ERROR` |
| `status == NRPC_STATUS_OK`, body status = 0 | `IO_SUCCESS` |
| `status == NRPC_STATUS_OK`, body status = 1 | `IO_ERROR` |
| `status == NRPC_STATUS_OK`, body status = 2 | `IO_NOT_FOUND` |
| `status == NRPC_STATUS_OK`, body status = 3 | `IO_NO_SPACE` |
| `status == NRPC_STATUS_OK`, body status = 4 | `IO_INVALID_PARAM` |
| Other `nrpc_status` values | `IO_ERROR` |

## Engine Changes Required

| Change | Reason |
|--------|--------|
| `obj_manager` expose `get_shard_count()` | STATFS aggregation |
| `obj_manager` expose per-shard statfs (or `io_statfs` per-shard callable via `spdk_thread_send_msg`) | STATFS aggregation |
| `storage_engine_ctx` add `rpc_core_index` field | RPC thread creation at startup |
| `storage_engine_ctx` add `rpc_port` field | RPC server port |
| Extend `validate_core_affinity` to check `num_shards + 1 (bg) + 1 (rpc)` | Core conflict detection |
| Engine startup creates RPC thread + `storage_server_start` after shard init | Integration |

## Testing

### Unit Tests (Mock Transport)

Test handler encoding/decoding and callback bridge logic using `nrpc_mock_transport_pair` within a single SPDK thread:

- `test/storage_server/test_rpc_handler.c` — each handler's request decode + response encode
- `test/storage_server/test_callback_bridge.c` — cross-thread reply queue simulation
- `test/storage_client/test_client.c` — client request encode + response decode + callback dispatch

### Integration Tests (TCP Transport + Real Engine)

**Prerequisite:** Real engine running with proper `config.json`, TCP transport for server and client.

#### Basic Operation Tests

All 8 operations covered:

| Test | Steps | Verification |
|------|-------|--------------|
| CREATE | create object | callback returns `IO_SUCCESS` |
| CREATE duplicate | create same object twice | second returns error |
| WRITE | create → write 4096 bytes at offset 0 | `IO_SUCCESS` |
| READ | create → write → read same range | data matches, length matches |
| READ non-existent | read non-existent object | `IO_NOT_FOUND` |
| DELETE | create → delete | `IO_SUCCESS` |
| DELETE non-existent | delete non-existent object | `IO_NOT_FOUND` |
| TRUNCATE | create → write → truncate to smaller size → read | read returns truncated size |
| PUNCH | create → write two ranges → punch middle → read ranges | punched range returns zeros/empty |
| CLONE | create src → write → clone to dst → read dst | dst data == src data |
| STATFS | query after create+write | total > 0, used > 0 |
| Large body WRITE | write exactly 1 MiB | `IO_SUCCESS`, data roundtrips |

#### Large-Scale IO Test

- **IO count:** 100,000 operations
- **Operations mix:** creates, writes, reads, deletes across multiple object IDs
- **Validation:** every operation completes with expected result (no hangs, no lost callbacks, responses match request by `request_id`)
- **Metrics:** total duration, throughput (IOPS), inflight queue behavior, reconnection handling

### Build & Run

```bash
make -C src/storage_server
make -C src/storage_client
cd test/storage_server && make run
```
