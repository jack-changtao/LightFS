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

/* ── storage_server library API ── */

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

  /* RPC pollers run on the RPC thread; app thread just waits for shutdown signal. */
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
