# LightFS Storage Server Refactor — Separating Engine from Network

**Date:** 2026-05-19
**Status:** Draft
**Builds on:** [2026-05-19-storage-server-design.md](2026-05-19-storage-server-design.md)

## Motivation

The current code has `storage_engine.c` doing too much — it orchestrates bdev init, mkfs/mount, shard init, obj_manager init, **and** RPC server creation all inside `storage_engine_start()`. The `main()` lives in `test_main.c` (a stress test file). The `storage_server.c` is just a passive library called by the engine.

This refactor gives each module a single responsibility:

- `storage_server.c` — SPDK app entry point (`main()`), loads config, starts networking/RPC, calls into engine for storage logic
- `storage_engine` — pure storage engine library (`libstorage_engine.a`), no network code

## Architecture

### Binary Layout

| Binary | Built from | Purpose |
|--------|-----------|---------|
| `storage_server` | `src/storage_server/` | Production SPDK app, RPC + engine |
| `libstorage_engine.a` | `src/engine/` | Static lib, engine logic only |
| `storage_engine_test` | `src/engine/test/` | Stress/benchmark binary (moved from `src/engine/src/test_main.c`) |

### Startup Flow

```
main()                              [storage_server.c]
  ├─ storage_engine_parse_argv()    (handles --mkfs, -m core_mask, -c config)
  ├─ load_config()                  ← reads engine/conf/config.json
  ├─ engine_log_init()
  └─ spdk_app_start(opts, storage_server_on_app_start, &ctx)
       │                            [SPDK app thread]
       └─ storage_server_on_app_start()
            ├─ load_config()        (already loaded, idempotent)
            ├─ init_spdk_bdev()
            ├─ storage_engine_mkfs_volume()   [--mkfs path]
            │    └─ spdk_app_stop(0)
            ├─ storage_engine_mount_volume()
            ├─ validate_core_affinity()
            ├─ shard_manager_init() + shard_subsystems_init()
            ├─ shard_manager_recover_on_mount()
            ├─ obj_manager_init()
            ├─ storage_server_create_rpc()    [NEW: RPC startup moved here]
            │    ├─ spdk_thread_create("rpc_thread")
            │    ├─ nrpc_tcp_transport_alloc()
            │    ├─ storage_server_create(thread, transport, obj_mgr)
            │    ├─ storage_server_start(srv, "0.0.0.0", rpc_port)
            │    └─ spdk_poller_register(poller_fn, srv, 0)
            └─ return
```

### What Moves Where

| Logic | From | To |
|-------|------|----|
| `main()` + SPDK app bootstrap | `src/engine/src/test_main.c` | `src/storage_server/storage_server.c` |
| RPC server creation (~40 lines) | `src/engine/src/storage_engine.c:665-710` | `src/storage_server/storage_server.c` |
| `ready_fn` callback pattern | `storage_engine_ctx_t` | Removed (RPC starts inline after engine init) |
| `rpc_core_index`, `rpc_port` fields | `storage_engine_ctx_t` | Removed (read from `config_t` by `storage_server.c`) |
| `storage_server`, `rpc_thread`, `rpc_transport` fields | `storage_engine_ctx_t` | Removed (become locals or fields in `storage_server.c`) |
| `test_main.c` | `src/engine/src/` | `src/engine/test/` (compiled as `storage_engine_test`) |

### Public Engine API (storage_engine.h)

```c
// Does bdev init + mkfs/mount + shard init + obj_manager init
// Replaces storage_engine_start(), minus RPC server creation
// Returns 0 on success, -1 on error
int storage_engine_init(storage_engine_ctx_t *ctx);

// Getter for obj_manager (used by storage_server to create RPC server)
obj_manager_t *storage_engine_get_obj_manager(storage_engine_ctx_t *ctx);

// Kept (unchanged):
//   storage_engine_parse_argv(), storage_engine_mkfs_volume(),
//   storage_engine_mount_volume(), storage_engine_stop(),
//   storage_engine_stop_async(), storage_engine_print_usage(),
//   get_storage_stats(), print_storage_stats()
```

### Fields Removed from storage_engine_ctx_t

```c
// REMOVED:
const char *core_mask;          // stays (used by storage_server for spdk_app_opts)
char *auto_core_mask;           // stays
const char *config_file;        // stays
spdk_event_fn ready_fn;         // REMOVED
void *ready_arg1;               // REMOVED
void *ready_arg2;               // REMOVED
int rpc_core_index;             // REMOVED
int rpc_port;                   // REMOVED
struct storage_server *storage_server;  // REMOVED
struct spdk_thread *rpc_thread;        // REMOVED
struct nrpc_transport *rpc_transport;  // REMOVED
```

## Config Changes

### New `rpc` section in config.json

```json
{
  "rpc": {
    "port": 8080,
    "core_index": 5
  }
}
```

### New struct in config.h

```c
struct {
  int port;           // 0 = RPC server disabled
  int core_index;     // dedicated core for RPC thread
} rpc;
```

### Config loading

- `config.c` adds `json_rpc_decoders[]` to parse the optional `"rpc"` object
- If the `"rpc"` key is missing, `port` defaults to 0 (no RPC server created)
- RPC settings are read by `storage_server.c` from `get_config()->rpc`

## File Layout After Refactor

```
src/
├── engine/
│   ├── Makefile                     # Builds libstorage_engine.a
│   ├── conf/
│   │   └── config.json              # + "rpc" section
│   ├── src/
│   │   ├── storage_engine.c         # RPC code removed; + storage_engine_init()
│   │   ├── storage_engine.h         # Public API (+ storage_engine_init, + getter)
│   │   ├── config.c                 # + rpc config decoding
│   │   ├── config.h                 # + rpc_config_t fields
│   │   └── ... (unchanged)
│   ├── log/                         # (unchanged)
│   └── test/
│       ├── test_main.c              # MOVED from src/test_main.c
│       └── Makefile                 # NEW: builds storage_engine_test
│
├── storage_server/
│   ├── Makefile                     # NEW: builds storage_server binary
│   ├── storage_server.c             # REWRITTEN: main() + SPDK bootstrap
│   ├── storage_server_internal.h    # Updated includes
│   ├── rpc_handler.c                # (unchanged)
│   └── callback_bridge.c            # (unchanged)
│
└── ... (other modules unchanged)
```

## Makefiles

### src/engine/Makefile — builds libstorage_engine.a

- Removes `src/test_main.c` from `C_SRCS`
- Removes `../storage_server/*.c` from `C_SRCS`
- Removes `../rpc/src/*.c` from `C_SRCS`
- Output: `libstorage_engine.a`

### src/storage_server/Makefile — builds storage_server

- `C_SRCS`: `storage_server.c`, `rpc_handler.c`, `callback_bridge.c`
- Links: `libstorage_engine.a`, `librpc.a` (or rpc sources directly)
- Dependencies: SPDK (event, bdev), RPC library, engine headers

### src/engine/test/Makefile — builds storage_engine_test

- `C_SRCS`: `test_main.c`
- Links: `libstorage_engine.a`, no RPC needed
- `test_main.c` is updated to call `storage_engine_init()` instead of `storage_engine_start()`

## Stop / Cleanup Path

Since RPC fields are removed from `storage_engine_ctx_t`, the RPC cleanup code currently in `storage_engine_stop()` and `storage_engine_stop_async()` (the `rpc_stop_cleanup` blocks) moves to `storage_server.c`:

### storage_server.c shutdown sequence

```
storage_server_on_shutdown()
  ├─ storage_server_destroy(srv)    // RPC server teardown
  ├─ nrpc_tcp_transport_free()      // TCP transport cleanup
  ├─ spdk_thread_exit(rpc_thread)   // RPC thread exit
  ├─ obj_manager_close()            // From storage_engine_stop()
  └─ storage_engine_stop_async()    // Shards + allocator + bdev cleanup
       └─ spdk_app_stop(rc)
```

### storage_engine_stop() — after refactor

- Removes the `rpc_stop_cleanup` block entirely
- Keeps: `obj_manager_close()`, `shard_manager_close()`, `allocator_close()`, `segment_release_bdev()`, `fini_spdk_bdev()`

### storage_engine_stop_async() — after refactor

- Removes the `rpc_stop_cleanup` block entirely
- Keeps: `obj_manager_close()`, then phase 1 (shard close async), then phase 2 (allocator + bdev)

## Error Handling

| Scenario | Behavior |
|----------|----------|
| `storage_engine_init()` fails | Call `spdk_app_stop(-1)` |
| `storage_server_create()` fails | Log error, stop app |
| `storage_server_start()` fails | Log error, stop app |
| RPC `port` is 0 in config | Skip RPC server creation (engine-only mode) |

## Testing

- Existing engine tests in `src/engine/test/` continue to work (they link against `libstorage_engine.a`)
- `storage_engine_test` binary can still run stress tests against the engine directly
- Storage server integration tests use the existing `test/storage_server/` framework
