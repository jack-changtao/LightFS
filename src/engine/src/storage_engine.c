#include "storage_engine.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <spdk/cpuset.h>
#include <spdk/event.h>
#include <spdk/thread.h>

#include "config.h"
#include "engine_log.h"
#include "segment/segment.h"
#include "superblock.h"

struct zero_seg_wait { volatile int done; int err; };

static void
zero_seg_write_cb(struct spdk_bdev_io *bdev_io, bool success, void *arg)
{
	struct zero_seg_wait *w = arg;
	w->err = success ? 0 : -EIO;
	w->done = 1;
	spdk_bdev_free_io(bdev_io);
}

int
storage_engine_mkfs_volume(storage_engine_ctx_t *ctx, const config_t *cfg,
			   int *num_shards_out)
{
	if (ctx == NULL || cfg == NULL || num_shards_out == NULL) {
		return -1;
	}
	*num_shards_out = cfg->shard.num_shards;
	if (allocator_mkfs(&ctx->allocator, cfg->segment.max_segments,
			       cfg->segment.segment_size)
	    != 0) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to mkfs space allocator");
		return -1;
	}
	/*
	 * Pre-populate open queues so alloc_segment() works during superblock_init_mkfs.
	 * This fills synchronously without needing update_queue.
	 * Strategy: 1 METADATA segment per shard (for shard_subsystems_init),
	 * then fill remaining segments into shard 0's DATA/JOURNAL queues.
	 */
	int n_shards = *num_shards_out;

	/*
	 * Step 1: Allocate METADATA segments.
	 * superblock_init_mkfs calls alloc_segment(allocator, 0, METADATA)
	 * n_shards times — all from per_shard[0]'s METADATA queue.
	 * shard_subsystems_init then calls alloc_segment(allocator, s, METADATA)
	 * once per shard s, from per_shard[s]'s METADATA queue.
	 */
	{
		/* n_shards distinct segments for superblock_init_mkfs, all in per_shard[0] */
		open_segment_queue_t *queue0 = &ctx->allocator.per_shard[0].open_queues[SEGMENT_TYPE_METADATA];
		int next = (int)(META_SEGMENT_SLOT_B + 1);
		for (int k = 0; k < n_shards; k++) {
			int idx = -1;
			for (int j = next; j < ctx->allocator.max_segments; j++) {
				if (ctx->allocator.segments[j].status == SEGMENT_STATUS_EMPTY) {
					idx = j;
					next = j + 1;
					break;
				}
			}
			if (idx == -1) {
				ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "No segments left for superblock metadata");
				return -1;
			}
			segment_t *seg = &ctx->allocator.segments[idx];
			seg->status = SEGMENT_STATUS_OPEN;
			seg->type = SEGMENT_TYPE_METADATA;
			ctx->allocator.used_segments++;
			ctx->allocator.is_persist_dirty = true;
			queue0->segments[queue0->tail] = (segment_id_t)idx;
			queue0->tail = (queue0->tail + 1) % MAX_OPEN_SEGMENTS;
		}
	}
	/* 1 distinct METADATA segment per shard for shard_subsystems_init */
	{
		int next = (int)(META_SEGMENT_SLOT_B + 1);
		for (int s = 0; s < n_shards; s++) {
			open_segment_queue_t *queue = &ctx->allocator.per_shard[s].open_queues[SEGMENT_TYPE_METADATA];
			int idx = -1;
			for (int j = next; j < ctx->allocator.max_segments; j++) {
				if (ctx->allocator.segments[j].status == SEGMENT_STATUS_EMPTY) {
					idx = j;
					next = j + 1;
					break;
				}
			}
			if (idx == -1) {
				ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "No segments left for shard %d metadata", s);
				return -1;
			}
			segment_t *seg = &ctx->allocator.segments[idx];
			seg->status = SEGMENT_STATUS_OPEN;
			seg->type = SEGMENT_TYPE_METADATA;
			ctx->allocator.used_segments++;
			ctx->allocator.is_persist_dirty = true;
			queue->segments[queue->tail] = (segment_id_t)idx;
			queue->tail = (queue->tail + 1) % MAX_OPEN_SEGMENTS;
		}
	}

	/* Step 2: Fill DATA and JOURNAL queues for ALL shards */
	{
		int next = (int)(META_SEGMENT_SLOT_B + 1);
		for (int s = 0; s < n_shards; s++) {
			for (int type = 1; type < SEGMENT_TYPE_COUNT; type++) {
				if (type == SEGMENT_TYPE_METADATA) {
					continue; /* already done per-shard above */
				}
				open_segment_queue_t *queue = &ctx->allocator.per_shard[s].open_queues[type];
				int target = (int)queue->refill_high;
				/* Cap at MAX_OPEN_SEGMENTS - 1 to avoid tail == head wrap ambiguity */
				if (target >= MAX_OPEN_SEGMENTS) {
					target = MAX_OPEN_SEGMENTS - 1;
				}
				uint16_t active = (uint16_t)((queue->tail + MAX_OPEN_SEGMENTS - queue->head) % MAX_OPEN_SEGMENTS);
				int need = target - (int)active;
				for (int i = 0; i < need; i++) {
					int idx = -1;
					for (int j = next; j < ctx->allocator.max_segments; j++) {
						if (ctx->allocator.segments[j].status == SEGMENT_STATUS_EMPTY) {
							idx = j;
							next = j + 1;
							break;
						}
					}
					if (idx == -1) {
						break;
					}
					segment_t *seg = &ctx->allocator.segments[idx];
					seg->status = SEGMENT_STATUS_OPEN;
					seg->type = (segment_type_t)type;
					ctx->allocator.used_segments++;
					ctx->allocator.is_persist_dirty = true;
					queue->segments[queue->tail] = (segment_id_t)idx;
					queue->tail = (queue->tail + 1) % MAX_OPEN_SEGMENTS;
				}
				ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "mkfs: shard=%d type=%d queue head=%u tail=%u",
					     s, type, (unsigned)queue->head, (unsigned)queue->tail);
			}
		}
	}
	/*
	 * Zero all JOURNAL and DATA segments on disk so that a subsequent mount
	 * (which scans JOURNAL segments for WAL replay) does not find stale
	 * records from a previous run on the same bdev.
	 */
	{
		struct spdk_bdev *bdev = get_bdev();
		struct spdk_bdev_desc *desc = get_bdev_desc();
		struct spdk_io_channel *ch = segment_bdev_io_channel();

		if (bdev && desc && ch) {
			uint32_t align = spdk_bdev_get_buf_align(bdev);
			void *zero_buf = spdk_dma_zmalloc(ctx->allocator.segment_size, align, NULL);

			if (zero_buf) {
				for (int i = (int)(META_SEGMENT_SLOT_B + 1); i < ctx->allocator.max_segments; i++) {
					segment_t *seg = &ctx->allocator.segments[i];
					if (seg->type != SEGMENT_TYPE_JOURNAL && seg->type != SEGMENT_TYPE_DATA) {
						continue;
					}

					struct zero_seg_wait w = {.done = 0, .err = -EIO};
					uint64_t bdev_off = (uint64_t)i * ctx->allocator.segment_size;

					spdk_bdev_write(desc, ch, zero_buf, bdev_off,
							ctx->allocator.segment_size,
							zero_seg_write_cb, &w);

					while (!w.done) {
						struct spdk_thread *cur = spdk_get_thread();
						if (cur) { (void)spdk_thread_poll(cur, 0, 0); }
						usleep(100);
					}
					if (w.err != 0) {
						ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to zero segment %d", i);
						spdk_dma_free(zero_buf);
						return -1;
					}
					/*
					 * Clear commit_seq so mount's WAL replay doesn't pick up
					 * stale sequence numbers from a previous run.  The segment
					 * data is zeroed above; without this, the in-memory
					 * first/last_commit_seq (restored from persisted allocator
					 * metadata) would cause WAL replay to attempt replaying
					 * non-existent records.
					 */
					if (seg->type == SEGMENT_TYPE_JOURNAL) {
						seg->first_commit_seq = 0;
						seg->last_commit_seq = 0;
					} else if (seg->type == SEGMENT_TYPE_DATA) {
						seg->last_commit_seq = 0;
					}
				}
				spdk_dma_free(zero_buf);
			}
		}
	}

	/*
	 * Persist allocator metadata with correct segment statuses (OPEN/DATA,
	 * OPEN/JOURNAL) that were set during queue filling above.  Without this,
	 * a subsequent mount sees all segments as EMPTY on disk.
	 */
	if (allocator_persist_sync(&ctx->allocator) != 0) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to persist allocator metadata after mkfs queue filling");
		return -1;
	}

	if (superblock_init_mkfs(&ctx->allocator, *num_shards_out) != 0) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "Failed to write initial superblock (mkfs)");
		return -1;
	}
	ENG_LOG_INFO(ENGINE_LOG_MOD_CORE,
		 "space allocator mkfs (--mkfs), superblock + shard count from config: %d",
		 *num_shards_out);
	return 0;
}

int
storage_engine_mount_volume(storage_engine_ctx_t *ctx, int *num_shards_out,
			    superblock_v1_t *out_sb)
{
	superblock_v1_t sb;

	if (ctx == NULL || num_shards_out == NULL) {
		return -1;
	}
	if (allocator_mount(&ctx->allocator) != 0) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
			 "mount failed (volume missing or not formatted); use --mkfs to create a new filesystem");
		return -1;
	}
	ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "space allocator mount done");
	if (superblock_load_current(&ctx->allocator, &sb, NULL) != 0) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
			 "mount: no valid engine superblock; cannot read shard count");
		return -1;
	}
	if (sb.shard_meta_num == 0 || sb.shard_meta_num > (uint32_t)MAX_SHARDS) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
			 "mount: invalid shard_meta_num %" PRIu32 " in superblock",
			 sb.shard_meta_num);
		return -1;
	}
	*num_shards_out = (int)sb.shard_meta_num;
	if (out_sb != NULL) {
		*out_sb = sb;
	}
	ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "shard count from superblock: %d", *num_shards_out);

	return 0;
}

void
storage_engine_stop(storage_engine_ctx_t *ctx)
{
	if (ctx == NULL) {
		return;
	}
	if (ctx->obj_manager != NULL) {
		obj_manager_close(ctx->obj_manager);
		ctx->obj_manager = NULL;
	}

	ctx->allocator.is_shutting_down = true;

	shard_manager_close(&ctx->shard_manager);

	allocator_close(&ctx->allocator);
	segment_release_bdev();
	fini_spdk_bdev();
	if (ctx->auto_core_mask) {
		free(ctx->auto_core_mask);
		ctx->auto_core_mask = NULL;
	}
}

typedef struct {
	storage_engine_ctx_t *ctx;
	storage_engine_stop_cb_t cb;
	void *cb_arg;
	struct spdk_thread *cb_thread;
} storage_stop_async_ctx_t;

static void
storage_engine_stop_async_phase2(void *arg)
{
	storage_stop_async_ctx_t *sctx = arg;
	storage_engine_ctx_t *ctx = sctx->ctx;

	allocator_close(&ctx->allocator);
	segment_release_bdev();
	fini_spdk_bdev();
	if (ctx->auto_core_mask) {
		free(ctx->auto_core_mask);
		ctx->auto_core_mask = NULL;
	}

	if (sctx->cb) {
		spdk_thread_send_msg(sctx->cb_thread, (spdk_msg_fn)sctx->cb, sctx->cb_arg);
	}
	free(sctx);
}

static void
storage_engine_stop_async_shards_done(void *arg)
{
	storage_stop_async_ctx_t *sctx = arg;
	storage_engine_ctx_t *ctx = sctx->ctx;

	/* Shard subsystems are closed (pollers unregistered on correct threads).
	 * Now drain any remaining inflight persist I/O before releasing the bdev. */
	{
		struct spdk_thread *cur = spdk_get_thread();
		int max_wait = 100;

		while (max_wait-- > 0 && ctx->allocator.is_persist_inflight) {
			if (cur) {
				(void)spdk_thread_poll(cur, 0, 0);
			}
			usleep(1000);
		}
	}

	/* Continue phase 2 on the same thread (the cb_thread). */
	storage_engine_stop_async_phase2(sctx);
}

void
storage_engine_stop_async(storage_engine_ctx_t *ctx,
			  storage_engine_stop_cb_t cb, void *cb_arg,
			  struct spdk_thread *cb_thread)
{
	storage_stop_async_ctx_t *sctx;

	if (ctx == NULL) {
		if (cb) cb(cb_arg);
		return;
	}

	sctx = malloc(sizeof(*sctx));
	if (!sctx) {
		if (cb) cb(cb_arg);
		return;
	}
	sctx->ctx = ctx;
	sctx->cb = cb;
	sctx->cb_arg = cb_arg;
	sctx->cb_thread = cb_thread ? cb_thread : spdk_get_thread();

	if (ctx->obj_manager != NULL) {
		obj_manager_close(ctx->obj_manager);
		ctx->obj_manager = NULL;
	}

	ctx->allocator.is_shutting_down = true;

	/* Phase 1: close shard subsystems on their threads (async). */
	shard_manager_close_async(&ctx->shard_manager,
				  storage_engine_stop_async_shards_done, sctx,
				  sctx->cb_thread);
}

int
get_storage_stats(allocator_t *allocator, storage_stats_t *stats)
{
  if (!allocator || !stats) {
    return -1;
  }

  allocator_statfs(allocator, &stats->total_size, &stats->used_size,
                         &stats->free_size, &stats->segment_count);
  stats->object_count = 0;
  stats->write_ops = 0;
  stats->read_ops = 0;
  stats->gc_count = 0;
  stats->gc_reclaimed = 0;

  return 0;
}

void
print_storage_stats(storage_stats_t *stats)
{
  if (!stats) {
    return;
  }

  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Storage Statistics:");
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Total Size: %lu GB", stats->total_size / (1024 * 1024 * 1024));
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Used Size: %lu GB", stats->used_size / (1024 * 1024 * 1024));
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Free Size: %lu GB", stats->free_size / (1024 * 1024 * 1024));
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Object Count: %lu", stats->object_count);
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Segment Count: %lu", stats->segment_count);
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Write Operations: %lu", stats->write_ops);
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "Read Operations: %lu", stats->read_ops);
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "GC Count: %lu", stats->gc_count);
  ENG_LOG_INFO(ENGINE_LOG_MOD_CORE, "GC Reclaimed: %lu GB",
           stats->gc_reclaimed / (1024 * 1024 * 1024));
}

void
storage_engine_print_usage(const char *prog)
{
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST, "Usage: %s [--mkfs] [-m <core_mask>] [-c <config>]", prog ? prog : "storage_engine");
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
		 "  (default)     Mount an existing volume, run engine demo; exits if not formatted.");
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
		 "  --mkfs        Format a new volume only (shard.num_shards), then exit successfully.");
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
		 "  -m <core_mask> SPDK core mask (e.g. 0xf for cores 0-3).");
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
		 "  -c <config>    App config file (default ./conf/config.json).");
}

int
storage_engine_parse_argv(int argc, char *argv[], storage_engine_ctx_t *out_ctx)
{
	if (out_ctx == NULL) {
		return 1;
	}
	out_ctx->force_mkfs = false;
	out_ctx->core_mask = NULL;
	out_ctx->config_file = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mkfs") == 0) {
			out_ctx->force_mkfs = true;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			storage_engine_print_usage(argv[0]);
			return -1;
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			out_ctx->core_mask = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			out_ctx->config_file = argv[++i];
		} else {
			ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE, "unknown argument: %s", argv[i]);
			storage_engine_print_usage(argv[0]);
			return 1;
		}
	}
	return 0;
}

static int
validate_core_affinity(storage_engine_ctx_t *ctx)
{
	const config_t *cfg = get_config();
	const struct spdk_cpuset *reactor_mask = spdk_app_get_core_mask();
	uint32_t reactor_count = spdk_cpuset_count(reactor_mask);
	int required_cores = cfg->shard.num_shards + 2; /* shards + bg + rpc */

	if (reactor_count < (uint32_t)required_cores) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
			"Core affinity validation failed: need %d cores (num_shards=%d + 1 bg_thread), "
			"but reactor_mask only provides %u cores",
			required_cores, cfg->shard.num_shards, reactor_count);
		return -1;
	}

	for (int i = 0; i < cfg->shard.core_map_len; i++) {
		if (!spdk_cpuset_get_cpu(reactor_mask, cfg->shard.core_map[i])) {
			ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
				"Core affinity validation failed: shard %d requires core %d, "
				"but that core is not in the reactor_mask",
				i, cfg->shard.core_map[i]);
			return -1;
		}
	}

	if (!spdk_cpuset_get_cpu(reactor_mask, cfg->shard.bg_core)) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
			"Core affinity validation failed: bg_thread requires core %d, "
			"but that core is not in the reactor_mask",
			cfg->shard.bg_core);
		return -1;
	}

	/* Check no duplicate cores */
	for (int i = 0; i < cfg->shard.core_map_len; i++) {
		if (cfg->shard.core_map[i] == cfg->shard.bg_core) {
			ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
				"Core affinity validation failed: shard %d and bg_thread "
				"both assigned to core %d (duplicate)",
				i, cfg->shard.bg_core);
			return -1;
		}
		for (int j = i + 1; j < cfg->shard.core_map_len; j++) {
			if (cfg->shard.core_map[i] == cfg->shard.core_map[j]) {
				ENG_LOG_ERROR(ENGINE_LOG_MOD_CORE,
					"Core affinity validation failed: shard %d and shard %d "
					"both assigned to core %d (duplicate)",
					i, j, cfg->shard.core_map[i]);
				return -1;
			}
		}
	}

	/* Validate RPC core */
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

	ENG_LOG_INFO(ENGINE_LOG_MOD_CORE,
		"Core affinity validated: %d shards, bg_thread on core %d, "
		"reactor_mask provides %u cores",
		cfg->shard.num_shards, cfg->shard.bg_core, reactor_count);
	return 0;
}

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

obj_manager_t *
storage_engine_get_obj_manager(storage_engine_ctx_t *ctx)
{
  return ctx ? ctx->obj_manager : NULL;
}

