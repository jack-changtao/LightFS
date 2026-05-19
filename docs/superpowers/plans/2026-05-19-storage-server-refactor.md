# Storage Server Refactor — Separate Engine from Network

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `storage_engine.c` to be a pure engine library (`libstorage_engine.a`), and make `storage_server.c` the SPDK app entry point (`main()`) with RPC/networking startup.

**Architecture:** `storage_server.c` becomes the SPDK app binary with `main()`. It loads config, calls `storage_engine_init()` for bdev/mkfs/mount/shards/obj_manager, then creates the RPC server. `storage_engine` is built as `libstorage_engine.a` with no network dependencies. `test_main.c` moves to `src/engine/test/` as `storage_engine_test`.

**Tech Stack:** C11, SPDK 26.01, JSON config via spdk_json, RPC via nrpc library

---

### Task 1: Add `rpc` config to `config.h`, `config.c`, and `config.json`

**Files:**
- Modify: `src/engine/src/config.h`
- Modify: `src/engine/src/config.c`
- Modify: `src/engine/conf/config.json`

- [ ] **Step 1: Add `rpc` struct to `config_t` in `config.h`**

In `src/engine/src/config.h`, add the rpc struct right before the closing `} config_t;` — after the `shard` struct and before `debug`:

```c
  struct {
    int num_shards;
    int bg_core;
    int *core_map;
    int core_map_len;
  } shard;
  struct {
    int port;
    int core_index;
  } rpc;
  struct {
    bool manifest_small_checkpoint;
  } debug;
} config_t;
```

- [ ] **Step 2: Add rpc section to `config.json`**

In `src/engine/conf/config.json`, add the `"rpc"` section after `"shard"` and before `"debug"`:

```json
  "shard": {
    "num_shards": 4
  },
  "rpc": {
    "port": 8080,
    "core_index": 5
  },
  "debug": {
    "manifest_small_checkpoint": false
  }
```

- [ ] **Step 3: Add JSON decoder struct and parse logic in `config.c`**

After the `json_shard` struct (around line 57), add:

```c
struct json_rpc {
  int32_t port;
  int32_t core_index;
};
```

After the `json_shard_decoders[]` array, add:

```c
static const struct spdk_json_object_decoder json_rpc_decoders[] = {
    {"port", offsetof(struct json_rpc, port), spdk_json_decode_int32, true},
    {"core_index", offsetof(struct json_rpc, core_index), spdk_json_decode_int32, true},
};
```

In `load_config()`, in the defaults section (near where `g_config.shard.bg_core = 0` is set, around line 349), add:

```c
  g_config.rpc.port = 0;
  g_config.rpc.core_index = 0;
```

After the shard parsing block (after the `// Validate core_map length matches num_shards` block, around line 573), add:

```c
  v = NULL;
  if (spdk_json_find(&values[0], "rpc", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) == 0 && v != NULL) {
    struct json_rpc jr;
    memset(&jr, 0, sizeof(jr));
    if (spdk_json_decode_object(v, json_rpc_decoders, SPDK_COUNTOF(json_rpc_decoders), &jr) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid rpc section in %s", path);
      ret = -EINVAL;
      goto out;
    }
    g_config.rpc.port = jr.port;
    g_config.rpc.core_index = jr.core_index;
  }
```

- [ ] **Step 4: Commit**

```bash
git add src/engine/src/config.h src/engine/src/config.c src/engine/conf/config.json
git commit -m "feat(config): add rpc section (port, core_index) to config

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: Update `storage_engine.h` — new API + remove RPC fields

**Files:**
- Modify: `src/engine/src/storage_engine.h`

- [ ] **Step 1: Remove RPC fields from `storage_engine_ctx_t`**

Remove from `storage_engine_ctx_t`:
- `spdk_event_fn ready_fn;`
- `void *ready_arg1;`
- `void *ready_arg2;`
- `int rpc_core_index;`
- `int rpc_port;`
- `struct storage_server *storage_server;`
- `struct spdk_thread *rpc_thread;`
- `struct nrpc_transport *rpc_transport;`

Also remove the forward declaration `struct storage_server;` near the top.

Also remove `#include "lightfs/storage_server/storage_server.h"` if present (it was in the .c but not the .h). Check and remove `#include "transport_tcp.h"` from the .c only.

The updated struct:

```c
typedef struct storage_engine_ctx {
	shard_manager_t shard_manager;
	allocator_t allocator;
	obj_manager_t *obj_manager;
	bool force_mkfs;
	const char *core_mask;
	char *auto_core_mask;
	const char *config_file;
} storage_engine_ctx_t;
```

- [ ] **Step 2: Add `storage_engine_init()` declaration**

Add the new function declaration after `storage_engine_parse_argv()`:

```c
/** Full engine init: bdev + mkfs/mount + shards + obj_manager.
 *  Replaces storage_engine_start() minus RPC server creation.
 *  Returns 0 on success, -1 on error (caller should stop the app). */
int storage_engine_init(storage_engine_ctx_t *ctx);
```

- [ ] **Step 3: Add `storage_engine_get_obj_manager()` getter**

```c
/** Returns the obj_manager after successful storage_engine_init(). */
obj_manager_t *storage_engine_get_obj_manager(storage_engine_ctx_t *ctx);
```

- [ ] **Step 4: Remove `storage_engine_start()` declaration**

Remove: `void storage_engine_start(void *arg);`

Since `storage_engine_init()` replaces it (called directly, not as an SPDK callback).

- [ ] **Step 5: Commit**

```bash
git add src/engine/src/storage_engine.h
git commit -m "refactor(storage_engine): add storage_engine_init() + getter, remove RPC fields

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 3: Update `storage_engine.c` — implement new API, remove RPC code

**Files:**
- Modify: `src/engine/src/storage_engine.c`

- [ ] **Step 1: Remove RPC includes and types**

Remove these includes from the top:
```c
#include "lightfs/storage_server/storage_server.h"
#include "transport_tcp.h"
```

Remove the `struct rpc_stop_ctx` definition and `rpc_stop_cleanup()` forward declaration (lines 20-25).

- [ ] **Step 2: Remove `rpc_stop_cleanup()` function** (lines 489-496)

Remove the entire function:
```c
static void
rpc_stop_cleanup(void *arg)
{
  struct rpc_stop_ctx *c = arg;
  storage_server_destroy(c->srv);
  nrpc_tcp_transport_free(c->transport);
  spdk_thread_exit(spdk_get_thread());
  free(c);
}
```

- [ ] **Step 3: Remove poller register functions** (lines 499-511)

Remove:
```c
static int
storage_server_poller_fn(void *arg) { ... }

static void
rpc_thread_register_poller(void *arg) { ... }
```

- [ ] **Step 4: Update `validate_core_affinity()` to include RPC core**

In `validate_core_affinity()`, the RPC core check block currently references `ctx->rpc_core_index`. Since RPC config now comes from `config_t`, read from config instead:

Change the RPC validation block (lines 568-586) from:
```c
  if (ctx->rpc_core_index >= 0) {
    if (!spdk_cpuset_get_cpu(reactor_mask, ctx->rpc_core_index)) {
      ...
      return -1;
    }
    for (int i = 0; i < cfg->shard.core_map_len; i++) {
      if (cfg->shard.core_map[i] == ctx->rpc_core_index) {
        ...
        return -1;
      }
    }
    if (cfg->shard.bg_core == ctx->rpc_core_index) {
      ...
      return -1;
    }
  }
```

To:
```c
  if (cfg->rpc.port > 0) {
    int rpc_core = cfg->rpc.core_index;
    if (!spdk_cpuset_get_cpu(reactor_mask, rpc_core)) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
        "RPC core %d not in reactor mask", rpc_core);
      return -1;
    }
    for (int i = 0; i < cfg->shard.core_map_len; i++) {
      if (cfg->shard.core_map[i] == rpc_core) {
        ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
          "RPC core %d conflicts with shard %d", rpc_core, i);
        return -1;
      }
    }
    if (cfg->shard.bg_core == rpc_core) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
        "RPC core %d conflicts with bg core", rpc_core);
      return -1;
    }
  }
```

- [ ] **Step 5: Rewrite `storage_engine_start()` into `storage_engine_init()`**

Replace the entire `storage_engine_start()` function. The key changes:
- Remove the `void *arg` signature — becomes `int storage_engine_init(storage_engine_ctx_t *ctx)`
- Remove the RPC server creation block (lines 665-710)
- Remove the `ready_fn` / `spdk_event_call` block at the end
- Return `0` on success, `-1` on error (caller handles `spdk_app_stop` / cleanup)
- On `--mkfs`, after mkfs succeeds, stop and return 0 (caller calls spdk_app_stop(0))

```c
int
storage_engine_init(storage_engine_ctx_t *ctx)
{
  config_t *cfg;
  int num_shards;

  if (load_config(ctx->config_file) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
       "Failed to load application config (default ./conf/config.json)");
    return -1;
  }
  cfg = get_config();

  if (init_spdk_bdev() != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
       "init_spdk_bdev failed (segment I/O requires backing bdev)");
    return -1;
  }
  segment_set_bdev(get_bdev());

  if (ctx->force_mkfs) {
    if (storage_engine_mkfs_volume(ctx, cfg, &num_shards) != 0) {
      return -1;
    }
    ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "mkfs finished successfully; exiting.");
    storage_engine_stop(ctx);
    return 0;
  }

  {
    superblock_v1_t sb = {0};

    if (storage_engine_mount_volume(ctx, &num_shards, &sb) != 0) {
      return -1;
    }

    if (validate_core_affinity(ctx) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Core affinity validation failed");
      return -1;
    }

    if (shard_manager_init(&ctx->shard_manager, num_shards, cfg->shard.core_map) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to initialize shard manager");
      return -1;
    }

    if (shard_subsystems_init(&ctx->shard_manager, &ctx->allocator, &sb) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to initialize per-shard subsystems");
      return -1;
    }
    if (shard_manager_recover_on_mount(&ctx->shard_manager, &ctx->allocator) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to recover per-shard state from mount path");
      return -1;
    }
  }

  if (obj_manager_init(&ctx->obj_manager, &ctx->shard_manager) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to initialize obj manager");
    return -1;
  }

  return 0;
}
```

- [ ] **Step 6: Remove RPC cleanup from `storage_engine_stop()`**

In `storage_engine_stop()`, remove the RPC cleanup block (lines 280-289):

Remove:
```c
  if (ctx->rpc_thread != NULL && ctx->storage_server != NULL) {
    struct rpc_stop_ctx *sc = malloc(sizeof(*sc));
    if (sc) {
      sc->srv = ctx->storage_server;
      sc->transport = ctx->rpc_transport;
      spdk_thread_send_msg(ctx->rpc_thread, rpc_stop_cleanup, sc);
    }
    ctx->rpc_thread = NULL;
    ctx->storage_server = NULL;
    ctx->rpc_transport = NULL;
  }
```

- [ ] **Step 7: Remove RPC cleanup from `storage_engine_stop_async()`**

In `storage_engine_stop_async()`, remove the RPC cleanup block (lines 383-393):

Remove:
```c
  if (ctx->rpc_thread != NULL && ctx->storage_server != NULL) {
    struct rpc_stop_ctx *sc = malloc(sizeof(*sc));
    if (sc) {
      sc->srv = ctx->storage_server;
      sc->transport = ctx->rpc_transport;
      spdk_thread_send_msg(ctx->rpc_thread, rpc_stop_cleanup, sc);
    }
    ctx->storage_server = NULL;
    ctx->rpc_transport = NULL;
    ctx->rpc_thread = NULL;
  }
```

- [ ] **Step 8: Add `storage_engine_get_obj_manager()` implementation**

```c
obj_manager_t *
storage_engine_get_obj_manager(storage_engine_ctx_t *ctx)
{
  return ctx ? ctx->obj_manager : NULL;
}
```

- [ ] **Step 9: Commit**

```bash
git add src/engine/src/storage_engine.c
git commit -m "refactor(storage_engine): implement storage_engine_init(), remove RPC code

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: Move `test_main.c` to `src/engine/test/` and update it

**Files:**
- Create: `src/engine/test/test_main.c` (move from `src/engine/src/test_main.c`)
- Delete: `src/engine/src/test_main.c` (after move)

- [ ] **Step 1: Copy the file**

```bash
cp src/engine/src/test_main.c src/engine/test/test_main.c
```

- [ ] **Step 2: Update includes in the new `test_main.c`**

The include paths are relative to `src/engine/src/` currently. Since the file now lives in `src/engine/test/`, the `#include "config.h"` style includes won't work unless -I points to src/. This will be handled by the Makefile's `-I` flags, so the includes can stay as-is.

The key change: `storage_engine_start` → `storage_engine_init`, and `ready_fn` → direct call.

Replace the `main()` function:

```c
int
main(int argc, char *argv[])
{
  storage_engine_ctx_t ctx;
  config_t *cfg;
  int prc;

  memset(&ctx, 0, sizeof(ctx));
  prc = storage_engine_parse_argv(argc, argv, &ctx);
  if (prc != 0) return prc < 0 ? 0 : prc;

  if (load_config(ctx.config_file) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "Failed to load config (%s)",
       ctx.config_file ? ctx.config_file : "./conf/config.json");
    return 1;
  }
  cfg = get_config();
  {
    struct engine_log_init_options log_opts = {0};
    engine_log_sink_mode_t sink_mode;
    if (map_log_sink_mode(cfg->log.mode, &sink_mode) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "Invalid log mode");
      return 1;
    }
    log_opts.sink_mode = sink_mode;
    log_opts.file_path = cfg->log.file_path;
    log_opts.default_level = (engine_log_level_t)cfg->log.default_level;
    log_opts.default_enabled = true;
    if (engine_log_init(&log_opts) != 0) return 1;
    if (engine_log_install_crash_handlers() != 0) {
      engine_log_finalize(); return 1;
    }
  }

  {
    struct spdk_app_opts opts;
    int ret;
    spdk_app_opts_init(&opts, sizeof(opts));
    opts.name = "storage_engine_test";
    opts.json_config_file = cfg->spdk_json_config_file;
    if (ctx.core_mask) {
      opts.reactor_mask = ctx.core_mask;
    } else {
      struct spdk_cpuset *needed_cores = spdk_cpuset_alloc();
      spdk_cpuset_zero(needed_cores);
      for (int i = 0; i < cfg->shard.core_map_len; i++) {
        spdk_cpuset_set_cpu(needed_cores, cfg->shard.core_map[i], true);
      }
      spdk_cpuset_set_cpu(needed_cores, cfg->shard.bg_core, true);
      ctx.auto_core_mask = strdup(spdk_cpuset_fmt(needed_cores));
      spdk_cpuset_free(needed_cores);
      opts.reactor_mask = ctx.auto_core_mask;
    }
    ctx.ready_fn = stress_test_start;
    ctx.ready_arg1 = &ctx;
    ctx.ready_arg2 = NULL;
    ret = spdk_app_start(&opts, storage_engine_start, &ctx);
    spdk_app_fini();
    if (ctx.auto_core_mask) free(ctx.auto_core_mask);
    engine_log_finalize();
    return ret;
  }
}
```

Wait — actually, `test_main.c` still uses `ready_fn` because it needs the stress test to start AFTER the engine init. But `storage_engine_init()` is called from `storage_engine_start` (the SPDK app callback). So for `test_main.c`, we keep `storage_engine_start` but updated.

Actually looking more carefully: `test_main.c` needs a different flow than `storage_server.c`. The test binary still uses `spdk_app_start()` with a callback, and that callback (`storage_engine_start`) does engine init + then fires the ready_fn (stress test).

So the approach for `test_main.c`:
- Keep the existing `spdk_app_start()` pattern
- Instead of calling `storage_engine_start`, we need a wrapper that calls `storage_engine_init()` and then fires the ready_fn
- OR: we keep a thin `storage_engine_start` as a compatibility wrapper

The cleanest approach: `test_main.c` gets its own small wrapper callback that does init + ready_fn:

Replace `main()` in `test_main.c`:

```c
static void
test_engine_start(void *arg)
{
  storage_engine_ctx_t *ctx = arg;

  if (storage_engine_init(ctx) != 0) {
    spdk_app_stop(-1);
    return;
  }

  if (ctx->force_mkfs) {
    spdk_app_stop(0);
    return;
  }

  stress_test_start(ctx, NULL);
}

int
main(int argc, char *argv[])
{
  storage_engine_ctx_t ctx;
  config_t *cfg;
  int prc;

  memset(&ctx, 0, sizeof(ctx));
  prc = storage_engine_parse_argv(argc, argv, &ctx);
  if (prc != 0) return prc < 0 ? 0 : prc;

  if (load_config(ctx.config_file) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "Failed to load config (%s)",
       ctx.config_file ? ctx.config_file : "./conf/config.json");
    return 1;
  }
  cfg = get_config();
  {
    struct engine_log_init_options log_opts = {0};
    engine_log_sink_mode_t sink_mode;
    if (map_log_sink_mode(cfg->log.mode, &sink_mode) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "Invalid log mode");
      return 1;
    }
    log_opts.sink_mode = sink_mode;
    log_opts.file_path = cfg->log.file_path;
    log_opts.default_level = (engine_log_level_t)cfg->log.default_level;
    log_opts.default_enabled = true;
    if (engine_log_init(&log_opts) != 0) return 1;
    if (engine_log_install_crash_handlers() != 0) {
      engine_log_finalize(); return 1;
    }
  }

  {
    struct spdk_app_opts opts;
    int ret;
    spdk_app_opts_init(&opts, sizeof(opts));
    opts.name = "storage_engine_test";
    opts.json_config_file = cfg->spdk_json_config_file;
    if (ctx.core_mask) {
      opts.reactor_mask = ctx.core_mask;
    } else {
      struct spdk_cpuset *needed_cores = spdk_cpuset_alloc();
      spdk_cpuset_zero(needed_cores);
      for (int i = 0; i < cfg->shard.core_map_len; i++) {
        spdk_cpuset_set_cpu(needed_cores, cfg->shard.core_map[i], true);
      }
      spdk_cpuset_set_cpu(needed_cores, cfg->shard.bg_core, true);
      ctx.auto_core_mask = strdup(spdk_cpuset_fmt(needed_cores));
      spdk_cpuset_free(needed_cores);
      opts.reactor_mask = ctx.auto_core_mask;
    }
    ret = spdk_app_start(&opts, test_engine_start, &ctx);
    spdk_app_fini();
    if (ctx.auto_core_mask) free(ctx.auto_core_mask);
    engine_log_finalize();
    return ret;
  }
}
```

- [ ] **Step 3: Remove `src/engine/src/test_main.c`**

```bash
git rm src/engine/src/test_main.c
git add src/engine/test/test_main.c
```

- [ ] **Step 4: Commit**

```bash
git commit -m "refactor: move test_main.c to src/engine/test/, update to use storage_engine_init()

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: Rewrite `storage_server.c` — add `main()` and SPDK bootstrap

**Files:**
- Modify: `src/storage_server/storage_server.c`

This is the biggest change. The file goes from a passive library to the SPDK app entry point.

- [ ] **Step 1: Write the new `storage_server.c`**

```c
#include "storage_server_internal.h"
#include "storage_engine.h"
#include "config.h"
#include "engine_log.h"
#include "transport_tcp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <spdk/cpuset.h>
#include <spdk/event.h>
#include <spdk/thread.h>

/* ── RPC server helpers ── */

static int
storage_server_poller_fn(void *arg)
{
  storage_server_poll((storage_server_t *)arg);
  return SPDK_POLLER_BUSY;
}

static void
rpc_thread_register_poller(void *arg)
{
  storage_server_t *srv = arg;
  spdk_poller_register(storage_server_poller_fn, srv, 0);
}

typedef struct {
  storage_server_t       *srv;
  struct nrpc_transport  *transport;
  struct spdk_thread     *rpc_thread;
} rpc_ctx_t;

static int
storage_server_create_rpc(storage_engine_ctx_t *eng_ctx, rpc_ctx_t *out)
{
  config_t *cfg = get_config();

  memset(out, 0, sizeof(*out));

  if (cfg->rpc.port <= 0) {
    return 0;
  }

  char thread_name[32];
  snprintf(thread_name, sizeof(thread_name), "rpc_thread");
  struct spdk_cpuset *rpc_cpuset = spdk_cpuset_alloc();
  spdk_cpuset_zero(rpc_cpuset);
  spdk_cpuset_set_cpu(rpc_cpuset, cfg->rpc.core_index, true);
  struct spdk_thread *rpc_thread = spdk_thread_create(thread_name, rpc_cpuset);
  spdk_cpuset_free(rpc_cpuset);

  if (!rpc_thread) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to create RPC thread");
    return -1;
  }

  struct nrpc_transport *transport = nrpc_tcp_transport_alloc();
  if (!transport) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to create TCP transport for RPC server");
    spdk_thread_exit(rpc_thread);
    spdk_thread_destroy(rpc_thread);
    return -1;
  }

  obj_manager_t *obj_mgr = storage_engine_get_obj_manager(eng_ctx);
  storage_server_t *srv = storage_server_create(rpc_thread, transport, obj_mgr);
  if (!srv) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to create storage server");
    nrpc_tcp_transport_free(transport);
    spdk_thread_exit(rpc_thread);
    spdk_thread_destroy(rpc_thread);
    return -1;
  }

  if (storage_server_start(srv, "0.0.0.0", (uint16_t)cfg->rpc.port) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to start storage server on port %d",
                  cfg->rpc.port);
    storage_server_destroy(srv);
    nrpc_tcp_transport_free(transport);
    spdk_thread_exit(rpc_thread);
    spdk_thread_destroy(rpc_thread);
    return -1;
  }

  SPDK_NOTICELOG("Storage RPC server started on port %d, core %d\n",
                 cfg->rpc.port, cfg->rpc.core_index);

  spdk_thread_send_msg(rpc_thread, rpc_thread_register_poller, srv);

  out->srv = srv;
  out->transport = transport;
  out->rpc_thread = rpc_thread;
  return 0;
}

/* ── shutdown ── */

struct shutdown_ctx {
  storage_engine_ctx_t *eng_ctx;
  rpc_ctx_t             rpc;
  int                   exit_code;
};

static void
shutdown_complete(void *arg)
{
  struct shutdown_ctx *sctx = arg;
  int rc = sctx->exit_code;
  free(sctx);
  spdk_app_stop(rc);
}

static void
do_shutdown(void *arg1, void *arg2)
{
  struct shutdown_ctx *sctx = arg1;
  (void)arg2;

  if (sctx->rpc.srv) {
    storage_server_destroy(sctx->rpc.srv);
  }
  if (sctx->rpc.transport) {
    nrpc_tcp_transport_free(sctx->rpc.transport);
  }
  if (sctx->rpc.rpc_thread) {
    spdk_thread_exit(sctx->rpc.rpc_thread);
    spdk_thread_destroy(sctx->rpc.rpc_thread);
  }

  storage_engine_stop_async(sctx->eng_ctx, shutdown_complete, sctx, spdk_get_thread());
}

static void
storage_server_request_shutdown(storage_engine_ctx_t *eng_ctx, rpc_ctx_t *rpc,
                                 int exit_code)
{
  struct shutdown_ctx *sctx = malloc(sizeof(*sctx));
  if (!sctx) {
    spdk_app_stop(-1);
    return;
  }
  sctx->eng_ctx = eng_ctx;
  sctx->rpc = *rpc;
  sctx->exit_code = exit_code;

  struct spdk_event *event = spdk_event_allocate(0, do_shutdown, sctx, NULL);
  if (!event) {
    spdk_app_stop(-1);
    return;
  }
  spdk_event_call(event);
}

/* ── SPDK app start callback ── */

static void
storage_server_on_app_start(void *arg)
{
  storage_engine_ctx_t *eng_ctx = arg;
  rpc_ctx_t rpc;

  if (storage_engine_init(eng_ctx) != 0) {
    spdk_app_stop(-1);
    return;
  }

  if (eng_ctx->force_mkfs) {
    spdk_app_stop(0);
    return;
  }

  if (storage_server_create_rpc(eng_ctx, &rpc) != 0) {
    storage_server_request_shutdown(eng_ctx, &rpc, -1);
    return;
  }

  /* RPC pollers run on the RPC thread; app thread just waits for shutdown signal.
   * Shutdown is triggered by signal handler (SIGINT/SIGTERM) via spdk_app_stop(0)
   * or by fatal error via storage_server_request_shutdown(). */
}

/* ── log helpers ── */

static int
map_log_sink_mode(const char *mode_str, engine_log_sink_mode_t *out_mode)
{
  if (!mode_str || !out_mode) return -1;
  if (strcmp(mode_str, "console") == 0) {
    *out_mode = ENGINE_LOG_SINK_CONSOLE; return 0;
  }
  if (strcmp(mode_str, "file") == 0) {
    *out_mode = ENGINE_LOG_SINK_FILE; return 0;
  }
  if (strcmp(mode_str, "both") == 0) {
    *out_mode = ENGINE_LOG_SINK_BOTH; return 0;
  }
  return -1;
}

/* ── main ── */

int
main(int argc, char *argv[])
{
  storage_engine_ctx_t eng_ctx;
  config_t *cfg;
  int prc;

  memset(&eng_ctx, 0, sizeof(eng_ctx));
  prc = storage_engine_parse_argv(argc, argv, &eng_ctx);
  if (prc != 0) return prc < 0 ? 0 : prc;

  /* Load config early so we can init logging before spdk_app_start */
  if (load_config(eng_ctx.config_file) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "Failed to load config (%s)",
                  eng_ctx.config_file ? eng_ctx.config_file : "./conf/config.json");
    return 1;
  }
  cfg = get_config();

  {
    struct engine_log_init_options log_opts = {0};
    engine_log_sink_mode_t sink_mode;
    if (map_log_sink_mode(cfg->log.mode, &sink_mode) != 0) {
      fprintf(stderr, "Invalid log mode in config\n");
      return 1;
    }
    log_opts.sink_mode = sink_mode;
    log_opts.file_path = cfg->log.file_path;
    log_opts.default_level = (engine_log_level_t)cfg->log.default_level;
    log_opts.default_enabled = true;
    if (engine_log_init(&log_opts) != 0) return 1;
    if (engine_log_install_crash_handlers() != 0) {
      engine_log_finalize(); return 1;
    }
  }

  {
    struct spdk_app_opts opts;
    int ret;
    spdk_app_opts_init(&opts, sizeof(opts));
    opts.name = "storage_server";
    opts.json_config_file = cfg->spdk_json_config_file;
    if (eng_ctx.core_mask) {
      opts.reactor_mask = eng_ctx.core_mask;
    } else {
      struct spdk_cpuset *needed_cores = spdk_cpuset_alloc();
      spdk_cpuset_zero(needed_cores);
      for (int i = 0; i < cfg->shard.core_map_len; i++) {
        spdk_cpuset_set_cpu(needed_cores, cfg->shard.core_map[i], true);
      }
      spdk_cpuset_set_cpu(needed_cores, cfg->shard.bg_core, true);
      eng_ctx.auto_core_mask = strdup(spdk_cpuset_fmt(needed_cores));
      spdk_cpuset_free(needed_cores);
      opts.reactor_mask = eng_ctx.auto_core_mask;
    }
    ret = spdk_app_start(&opts, storage_server_on_app_start, &eng_ctx);
    spdk_app_fini();
    if (eng_ctx.auto_core_mask) free(eng_ctx.auto_core_mask);
    engine_log_finalize();
    return ret;
  }
}
```

- [ ] **Step 2: Commit**

```bash
git add src/storage_server/storage_server.c
git commit -m "refactor(storage_server): add main() and SPDK app bootstrap

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 6: Update `storage_server_internal.h`

**Files:**
- Modify: `src/storage_server/storage_server_internal.h`

- [ ] **Step 1: Update includes**

The current file includes `"io_types.h"`, `"obj.h"`, `"shard.h"` from engine. These paths need to be adjusted for the storage_server Makefile's include paths. Since the Makefile will add `-I` flags pointing to `src/engine/src/`, the include paths stay as-is for the engine sources.

But `#include "lightfs/storage_server/storage_server.h"` needs the `-I$(LIGHTFS_ROOT)/include` flag.

Also add `#include "storage_engine.h"` if needed by the structs (it's not — the types used are forward-declared or come from other headers).

Actually, examining the current `storage_server_internal.h`, the `storage_server` struct has no RPC fields in it — the `storage_server_t` only has `rpc_thread`, `nrpc`, `transport`, `obj_mgr`, `host`, `port`. Those are all fine. No changes needed to the struct.

But `bridge_ctx_t` and `reply_msg_t` use types from `"server.h"` (nrpc types) and `"transport.h"`. Those are RPC lib headers.

The only change: remove `#include "lightfs/storage_server/storage_server.h"` — actually no, that's the public header, still needed for the `storage_server_t` typedef and the public API declarations.

Actually this file doesn't need changes. It already has the right includes. Let me double-check.

Current includes:
```c
#include "lightfs/storage_server/storage_server.h"
#include "server.h"
#include "transport.h"
#include "io_types.h"
#include "obj.h"
#include "shard.h"
#include <spdk/thread.h>
```

These are all fine. `io_types.h`, `obj.h`, `shard.h` are engine headers. `server.h`, `transport.h` are RPC headers. The `-I` flags in the Makefile will resolve them.

**No changes needed to this file.**

- [ ] **Step 1: No changes — skip**

---

### Task 7: Update `src/engine/Makefile` — build `libstorage_engine.a`

**Files:**
- Modify: `src/engine/Makefile`

- [ ] **Step 1: Change Makefile from building `storage_engine` binary to `libstorage_engine.a`**

```makefile
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

# Build static library instead of app binary
LIB := storage_engine

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)

C_SRCS := \
	src/storage_engine.c \
	log/engine_log.c \
	src/shard.c \
	src/segment/alloc.c \
	src/segment/segment.c \
	src/segment/segment_cache.c \
	src/segment/update_queue.c \
	src/segment/reclaim.c \
	src/crc32c.c \
	src/superblock.c \
	src/shard_meta.c \
	src/wal.c \
	src/manifest.c \
	src/io.c \
	src/obj.c \
	src/gc.c \
	src/checkpoint.c \
	src/config.c

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk

CFLAGS += -I$(abspath .)/src
CFLAGS += -I$(abspath .)/log
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
```

Key changes from the old Makefile:
- `APP := storage_engine` → `LIB := storage_engine`
- `include $(SPDK_ROOT_DIR)/mk/spdk.app.mk` → `include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk`
- Remove `src/test_main.c` from `C_SRCS`
- Remove `../storage_server/*.c` from `C_SRCS`
- Remove `../rpc/src/*.c` from `C_SRCS`
- Remove `RPC_ROOT` variable (no longer needed)
- Remove `SPDK_LIB_LIST` (only needed for app builds)
- Remove `-I$(RPC_ROOT)/src` (no longer needed)
- Remove `-I$(LIGHTFS_ROOT)/src/storage_server` (no longer needed)

- [ ] **Step 2: Commit**

```bash
git add src/engine/Makefile
git commit -m "build(engine): change Makefile to build libstorage_engine.a

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8: Create `src/storage_server/Makefile`

**Files:**
- Create: `src/storage_server/Makefile`

- [ ] **Step 1: Write `src/storage_server/Makefile`**

```makefile
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP := storage_server

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)
ENGINE_DIR := $(LIGHTFS_ROOT)/src/engine
RPC_DIR    := $(LIGHTFS_ROOT)/src/rpc

C_SRCS := \
	storage_server.c \
	rpc_handler.c \
	callback_bridge.c \
	$(RPC_DIR)/src/server.c \
	$(RPC_DIR)/src/frame.c \
	$(RPC_DIR)/src/transport_tcp.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

# Link against libstorage_engine.a
LIBS += -L$(ENGINE_DIR) -lstorage_engine

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

CFLAGS += -I$(ENGINE_DIR)/src
CFLAGS += -I$(ENGINE_DIR)/log
CFLAGS += -I$(RPC_DIR)/src
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
```

- [ ] **Step 2: Commit**

```bash
git add src/storage_server/Makefile
git commit -m "build(storage_server): add Makefile for storage_server binary

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 9: Create `src/engine/test/Makefile` for `storage_engine_test`

**Files:**
- Create: `src/engine/test/Makefile`

- [ ] **Step 1: Write `src/engine/test/Makefile`**

```makefile
#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP := storage_engine_test

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)
ENGINE_DIR  := $(LIGHTFS_ROOT)/src/engine

C_SRCS := test_main.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

LIBS += -L$(ENGINE_DIR) -lstorage_engine

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

CFLAGS += -I$(ENGINE_DIR)/src
CFLAGS += -I$(ENGINE_DIR)/log
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
```

- [ ] **Step 2: Commit**

```bash
git add src/engine/test/Makefile
git commit -m "build(engine): add Makefile for storage_engine_test binary

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 10: Build verification

- [ ] **Step 1: Build `libstorage_engine.a`**

```bash
make -C src/engine clean && make -C src/engine
```

Expected: `libstorage_engine.a` produced in `src/engine/`. No errors.

- [ ] **Step 2: Build `storage_server`**

```bash
make -C src/storage_server clean && make -C src/storage_server
```

Expected: `storage_server` binary produced. No errors.

- [ ] **Step 3: Build `storage_engine_test`**

```bash
make -C src/engine/test clean && make -C src/engine/test
```

Expected: `storage_engine_test` binary produced. No errors.

- [ ] **Step 4: Commit any build fixes**

```bash
git add -A
git commit -m "fix: build verification fixes

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```
