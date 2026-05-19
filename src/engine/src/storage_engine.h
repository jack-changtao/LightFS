#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include <stdbool.h>

#include "checkpoint.h"
#include "config.h"
#include "obj.h"
#include "segment/segment.h"
#include "shard.h"

#include <spdk/event.h>

struct storage_server;

typedef struct storage_engine_ctx {
	shard_manager_t shard_manager;
	allocator_t allocator;
	obj_manager_t *obj_manager;
	bool force_mkfs;
	/** SPDK core mask (e.g. "0xf" for cores 0-3); NULL = SPDK default. */
	const char *core_mask;
	/** Auto-derived core mask from core_map + bg_core; freed on cleanup. */
	char *auto_core_mask;
	/** App config file path; NULL = default ./conf/config.json. */
	const char *config_file;
	/** After successful mount init, scheduled on the app thread (ignored for --mkfs). */
	spdk_event_fn ready_fn;
	void *ready_arg1;
	void *ready_arg2;
	/** RPC server configuration */
	int rpc_core_index;
	int rpc_port;
	struct storage_server *storage_server;
	struct spdk_thread *rpc_thread;
	struct nrpc_transport *rpc_transport;
} storage_engine_ctx_t;

void storage_engine_print_usage(const char *prog);

/** Returns 0 on success; 1 on bad args; -1 if user asked for --help (caller should exit 0). */
int storage_engine_parse_argv(int argc, char *argv[], storage_engine_ctx_t *out_ctx);

int storage_engine_mkfs_volume(storage_engine_ctx_t *ctx, const config_t *cfg,
			       int *num_shards_out);
int storage_engine_mount_volume(storage_engine_ctx_t *ctx, int *num_shards_out,
			       superblock_v1_t *out_sb);
void storage_engine_stop(storage_engine_ctx_t *ctx);

/** Async stop: closes shard subsystems on their threads first, then finishes
 *  cleanup (allocator, bdev) and calls `cb(cb_arg)` on `cb_thread`. */
typedef void (*storage_engine_stop_cb_t)(void *cb_arg);
void storage_engine_stop_async(storage_engine_ctx_t *ctx,
			       storage_engine_stop_cb_t cb, void *cb_arg,
			       struct spdk_thread *cb_thread);

void storage_engine_start(void *arg);

/* 获取存储引擎全局统计信息（segment 层数据来自 allocator） */
int get_storage_stats(allocator_t *allocator, storage_stats_t *stats);
void print_storage_stats(storage_stats_t *stats);

#endif
