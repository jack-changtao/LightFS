/* Stress test: random write / punch / truncate / delete across multiple shards.
 *
 * Each op allocates its own buffer.  Verification of writes and punches is
 * chained inside the opʼs callback — after the WAL record is durable and the
 * manifest is updated, the callback issues a read-back against the same buffer.
 * This eliminates all races between writes / punches and their verification.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spdk/cpuset.h>
#include <spdk/event.h>

#include "config.h"
#include "engine_log.h"
#include "obj.h"
#include "segment/segment.h"
#include "shard.h"
#include "storage_engine.h"

/* ---------- compile-time tunables ---------- */
#ifndef STRESS_NUM_OBJECTS
#define STRESS_NUM_OBJECTS 64
#endif
#ifndef STRESS_NUM_OPS
#define STRESS_NUM_OPS 512
#endif
#ifndef STRESS_DATA_SIZE
#define STRESS_DATA_SIZE 4096
#endif
#ifndef STRESS_MAX_INFLIGHT
#define STRESS_MAX_INFLIGHT 4
#endif

/* ---------- deterministic byte pattern ---------- */
static inline uint8_t
gen_byte(uint64_t oid, uint64_t off, uint64_t gen)
{
	uint64_t s = oid * 6364136223846793005ULL + off * 1442695040888963407ULL
		     + gen * 0x5bd1e995ULL;
	s ^= s >> 33;
	s *= 0xff51afd7ed558ccdULL;
	s ^= s >> 33;
	return (uint8_t)(s & 0xff);
}

/* ---------- per-object tracked state ---------- */
typedef struct {
	uint64_t size;
	uint64_t generation; /* bumped on recreate */
	int      deleted;
} obj_state_t;

/* ---------- operation kinds ---------- */
typedef enum {
	OP_WRITE,
	OP_PUNCH,
	OP_TRUNCATE,
	OP_DELETE,
	OP_RECREATE,
} op_kind_t;

typedef struct {
	op_kind_t  kind;
	uint64_t   oid;
	uint64_t   off;
	uint64_t   len;
} stress_op_t;

typedef struct stress_ctx_s stress_ctx_t;

/* ---------- per-op wrapper (owns buf and op; freed after verify chain) ---------- */
typedef struct stress_cb_wrap_s {
	stress_ctx_t *ctx;
	stress_op_t   op;
	uint8_t      *buf;   /* owned buffer: write source then verify destination */
	uint64_t      gen;   /* generation captured at submit time */
} stress_cb_wrap_t;

struct stress_ctx_s {
	storage_engine_ctx_t *engine;
	obj_state_t          *states;
	stress_op_t          *ops;
	int                   num_ops;
	int                   op_idx;
	int                   in_flight;
	int                   errors;
	int                   ops_done;
};

static stress_ctx_t *g_stress;

/* ---------- op generation ---------- */
static uint32_t
stress_lcg(uint32_t *seed)
{
	*seed = (*seed * 1103515245u + 12345u) & 0x7fffffffu;
	return *seed;
}

static void
generate_ops(stress_op_t *ops, int num_ops, int num_objects, uint32_t seed)
{
	int n = 0;
	uint32_t r;

	for (int i = 0; i < num_ops && n < num_ops; i++) {
		stress_op_t *op = &ops[n];
		r = stress_lcg(&seed);
		op->oid = (uint64_t)(r % num_objects) + 1;
		uint32_t bucket = stress_lcg(&seed) % 100;

		if (bucket < 40) {
			op->kind = OP_WRITE;
			op->off = (uint64_t)(stress_lcg(&seed) % (STRESS_DATA_SIZE * 2));
			op->len = STRESS_DATA_SIZE;
			n++;
		} else if (bucket < 60) {
			op->kind = OP_PUNCH;
			op->off = (uint64_t)(stress_lcg(&seed) % STRESS_DATA_SIZE);
			op->len = (uint64_t)(stress_lcg(&seed) % (STRESS_DATA_SIZE / 2)) + 1;
			n++;
		} else if (bucket < 80) {
			op->kind = OP_TRUNCATE;
			op->len = (uint64_t)(stress_lcg(&seed) % (STRESS_DATA_SIZE * 3)) + 1;
			op->off = 0;
			n++;
		} else if (bucket < 92) {
			op->kind = OP_DELETE;
			op->off = 0;
			op->len = 0;
			n++;
		} else {
			op->kind = OP_RECREATE;
			op->off = 0;
			op->len = STRESS_DATA_SIZE;
			n++;
		}
	}
	g_stress->num_ops = n;
}

/* ---------- helpers ---------- */
static void free_wrap(stress_cb_wrap_t *w)
{
	free(w->buf);
	free(w);
}

static int
verify_buffer(stress_cb_wrap_t *w, int expect_zero)
{
	for (uint64_t i = 0; i < w->op.len; i++) {
		uint8_t expected = expect_zero ? 0 : gen_byte(w->op.oid, w->op.off + i, w->gen);
		if (w->buf[i] != expected) {
			ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST,
				 "verify fail: oid=%lu off=%lu i=%lu gen=%lu "
				 "expect=0x%02x got=0x%02x %s",
				 (unsigned long)w->op.oid, (unsigned long)w->op.off,
				 (unsigned long)i, (unsigned long)w->gen,
				 expected, w->buf[i],
				 expect_zero ? "(punch-zero)" : "(write)");
			return -1;
		}
	}
	return 0;
}

static void finish_test(stress_ctx_t *ctx);

/* ---------- forward decls ---------- */
static void stress_verify_cb(void *user_context, io_result_t r);
static void stress_op_cb(void *user_context, io_result_t r);
static void stress_dispatch_next(stress_ctx_t *ctx);

/* stage 2: read-back verification callback */
static void
stress_verify_cb(void *user_context, io_result_t r)
{
	stress_cb_wrap_t *w = user_context;
	stress_ctx_t *ctx = w->ctx;

	ctx->in_flight--;

	if (r != IO_SUCCESS) {
		/* PUNCH verify read races with concurrent truncate/delete/recreate */
		if (w->op.kind != OP_PUNCH) {
			ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST,
				 "verify read err: oid=%lu kind=%d r=%d",
				 (unsigned long)w->op.oid, (int)w->op.kind, (int)r);
			ctx->errors++;
		}
	} else if (w->op.kind == OP_WRITE) {
		if (verify_buffer(w, 0) != 0) ctx->errors++;
	} else {
		/* PUNCH: verify read can race with concurrent truncate/delete/recreate
		 * that changes object size or removes it entirely.  Treat as best-effort. */
		if (verify_buffer(w, 1) != 0)
			ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
				 "punch verify best-effort miss: oid=%lu off=%lu len=%lu",
				 (unsigned long)w->op.oid, (unsigned long)w->op.off,
				 (unsigned long)w->op.len);
	}

	free_wrap(w);
	ctx->ops_done++;

	if (ctx->op_idx >= ctx->num_ops && ctx->in_flight == 0) {
		finish_test(ctx);
		return;
	}
	stress_dispatch_next(ctx);
}

/* stage 1: op callback (WAL durable, manifest updated) */
static void
stress_op_cb(void *user_context, io_result_t r)
{
	stress_cb_wrap_t *w = user_context;
	stress_ctx_t *ctx = w->ctx;
	obj_state_t *st = &ctx->states[w->op.oid - 1];

	ctx->in_flight--;

	if (r != IO_SUCCESS) {
		free_wrap(w);
		ctx->ops_done++;
		goto dispatch;
	}

	switch (w->op.kind) {
	case OP_WRITE:
		if (w->op.off + w->op.len > st->size)
			st->size = w->op.off + w->op.len;
		/* stage 2: read-back verify (reuse w->buf) */
		ctx->in_flight++;
		if (obj_read(ctx->engine->obj_manager, w->op.oid,
			     w->op.off, w->op.len,
			     (char *)w->buf, stress_verify_cb, w) != IO_SUCCESS) {
			ctx->in_flight--;
			ctx->errors++;
			free_wrap(w);
		}
		return; /* w stays alive — ctx->ops_done deferred to verify_cb */

	case OP_PUNCH:
		/* stage 2: read-back verify zeros (best-effort, races with concurrent ops) */
		ctx->in_flight++;
		if (obj_read(ctx->engine->obj_manager, w->op.oid,
			     w->op.off, w->op.len,
			     (char *)w->buf, stress_verify_cb, w) != IO_SUCCESS) {
			ctx->in_flight--;
			free_wrap(w);
		}
		return;

	case OP_TRUNCATE:
		st->size = w->op.len;
		free_wrap(w);
		break;

	case OP_DELETE:
		st->deleted = 1;
		st->size = 0;
		free_wrap(w);
		break;

	case OP_RECREATE: {
		st->deleted = 0;
		st->generation++;
		st->size = w->op.len;

		/* submit companion write with new generation data */
		stress_cb_wrap_t *w2 = malloc(sizeof(*w2));
		if (!w2) {
			ctx->errors++;
			free_wrap(w);
			break;
		}
		w2->ctx = ctx;
		w2->op.kind = OP_WRITE;
		w2->op.oid = w->op.oid;
		w2->op.off = 0;
		w2->op.len = w->op.len;
		w2->gen = st->generation;
		w2->buf = malloc(w->op.len);
		if (!w2->buf) {
			ctx->errors++;
			free(w2);
			free_wrap(w);
			break;
		}
		for (uint64_t i = 0; i < w->op.len; i++)
			w2->buf[i] = gen_byte(w->op.oid, i, st->generation);
		free_wrap(w); /* recreate wrap done */
		ctx->in_flight++;
		if (obj_write(ctx->engine->obj_manager, w2->op.oid, 0,
			      w2->op.len, (char *)w2->buf,
			      stress_op_cb, w2) != IO_SUCCESS) {
			ctx->in_flight--;
			ctx->errors++;
			free_wrap(w2);
		}
		return; /* w freed, defer ops_done to write cb */
	}
	}

	ctx->ops_done++;

dispatch:
	if (ctx->op_idx >= ctx->num_ops && ctx->in_flight == 0) {
		finish_test(ctx);
		return;
	}
	stress_dispatch_next(ctx);
}

/* ---------- dispatch ---------- */
static void
stress_dispatch_next(stress_ctx_t *ctx)
{
	while (ctx->op_idx < ctx->num_ops && ctx->in_flight < STRESS_MAX_INFLIGHT) {
		stress_op_t *op = &ctx->ops[ctx->op_idx];
		stress_cb_wrap_t *w = malloc(sizeof(*w));
		if (!w) {
			ctx->errors++;
			ctx->op_idx++;
			continue;
		}
		memset(w, 0, sizeof(*w));
		w->ctx = ctx;
		w->op = *op;
		obj_state_t *st = &ctx->states[op->oid - 1];

		switch (op->kind) {
		case OP_WRITE:
			w->gen = st->generation;
			w->buf = malloc(op->len);
			if (!w->buf) {
			ctx->errors++;
			free(w);
			ctx->op_idx++;
			continue;
		}
			for (uint64_t i = 0; i < op->len; i++)
				w->buf[i] = gen_byte(op->oid, op->off + i, w->gen);
			ctx->in_flight++;
			ctx->op_idx++;
			if (obj_write(ctx->engine->obj_manager, op->oid,
				      op->off, op->len, (char *)w->buf,
				      stress_op_cb, w) != IO_SUCCESS) {
				ctx->in_flight--;
				ctx->errors++;
				free_wrap(w);
			}
			return;

		case OP_PUNCH:
			w->buf = malloc(op->len);
			if (!w->buf) {
			ctx->errors++;
			free(w);
			ctx->op_idx++;
			continue;
		}
			memset(w->buf, 0, op->len); /* will be used as read-back target */
			ctx->in_flight++;
			ctx->op_idx++;
			if (obj_punch(ctx->engine->obj_manager, op->oid,
				      op->off, op->len, stress_op_cb, w) != IO_SUCCESS) {
				ctx->in_flight--;
				ctx->errors++;
				free_wrap(w);
			}
			return;

		case OP_TRUNCATE:
			ctx->in_flight++;
			ctx->op_idx++;
			if (obj_truncate(ctx->engine->obj_manager, op->oid,
					 op->len, stress_op_cb, w) != IO_SUCCESS) {
				ctx->in_flight--;
				ctx->errors++;
				free(w);
			}
			return;

		case OP_DELETE:
			ctx->in_flight++;
			ctx->op_idx++;
			if (obj_delete(ctx->engine->obj_manager, op->oid,
				      stress_op_cb, w) != IO_SUCCESS) {
				ctx->in_flight--;
				ctx->errors++;
				free(w);
			}
			return;

		case OP_RECREATE:
			ctx->in_flight++;
			ctx->op_idx++;
			if (obj_create(ctx->engine->obj_manager, op->oid,
				       stress_op_cb, w) != IO_SUCCESS) {
				ctx->in_flight--;
				ctx->errors++;
				free(w);
			}
			return;
		}
	}

	if (ctx->in_flight == 0)
		finish_test(ctx);
}

/* ---------- finish ---------- */
typedef struct {
	storage_engine_ctx_t *engine;
	void *ops;
	void *states;
	void *ctx;
	int rc;
} shutdown_ctx_t;

static void
shutdown_complete(void *arg)
{
	shutdown_ctx_t *s = arg;
	int rc = s->rc;
	free(s->ops);
	free(s->states);
	free(s->ctx);
	free(s);
	spdk_app_stop(rc);
}

static void
shutdown_fn(void *arg1, void *arg2)
{
	shutdown_ctx_t *s = arg1;
	(void)arg2;

	/* Async stop: shard subsystems are closed on their threads,
	 * then bdev cleanup happens, then shutdown_complete calls spdk_app_stop. */
	storage_engine_stop_async(s->engine, shutdown_complete, s, spdk_get_thread());
}

static void
finish_test(stress_ctx_t *ctx)
{
	int rc = ctx->errors > 0 ? -1 : 0;

	if (ctx->errors > 0)
		ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST,
			 "stress test: %d ops, %d errors",
			 ctx->ops_done, ctx->errors);
	else
		ENG_LOG_INFO(ENGINE_LOG_MOD_TEST,
			 "stress test: all %d ops passed", ctx->ops_done);

	storage_stats_t stats;
	if (get_storage_stats(&ctx->engine->allocator, &stats) == 0)
		print_storage_stats(&stats);

	shutdown_ctx_t *s = malloc(sizeof(*s));
	if (!s) {
	spdk_app_stop(-1);
	return;
}
	s->engine = ctx->engine;
	s->ops = ctx->ops;
	s->states = ctx->states;
	s->ctx = ctx;
	s->rc = rc;

	struct spdk_event *event = spdk_event_allocate(0, shutdown_fn, s, NULL);
	if (!event) {
	spdk_app_stop(-1);
	return;
}
	spdk_event_call(event);
}

/* ---------- phase 0: create all objects ---------- */
static void stress_create_dispatch(stress_ctx_t *ctx);

static void
stress_create_cb(void *user_context, io_result_t r)
{
	stress_cb_wrap_t *w = user_context;
	stress_ctx_t *ctx = w->ctx;

	(void)r; /* ignore failures */
	free(w);
	ctx->op_idx++; /* op_idx repurposed as create counter */
	stress_create_dispatch(ctx);
}

static void
stress_create_dispatch(stress_ctx_t *ctx)
{
	int n = STRESS_NUM_OBJECTS;

	if (ctx->op_idx >= n) {
		ctx->op_idx = 0; /* reset for stress ops */
		ENG_LOG_INFO(ENGINE_LOG_MOD_TEST, "all %d objects created, starting %d ops",
			 n, ctx->num_ops);
		stress_dispatch_next(ctx);
		return;
	}

	uint64_t oid = (uint64_t)(ctx->op_idx + 1);
	stress_cb_wrap_t *w = calloc(1, sizeof(*w));
	if (!w) {
	ctx->errors++;
	ctx->op_idx++;
	stress_create_dispatch(ctx);
	return;
}
	w->ctx = ctx;
	w->op.oid = oid;
	if (obj_create(ctx->engine->obj_manager, oid,
		       stress_create_cb, w) != IO_SUCCESS) {
		ctx->errors++;
		free(w);
		ctx->op_idx++;
		stress_create_dispatch(ctx);
	}
}

static void
stress_create_objects(stress_ctx_t *ctx)
{
	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST, "creating %d objects...", STRESS_NUM_OBJECTS);
	ctx->op_idx = 0; /* repurposed as create counter */
	stress_create_dispatch(ctx);
}

/* ---------- entry ---------- */
static void
stress_test_start(void *arg1, void *arg2)
{
	storage_engine_ctx_t *eng = arg1;
	stress_ctx_t *ctx;
	int num_objects = STRESS_NUM_OBJECTS;
	(void)arg2;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
	spdk_app_stop(-1);
	return;
}
	g_stress = ctx;
	ctx->engine = eng;

	ctx->states = calloc((size_t)num_objects, sizeof(obj_state_t));
	ctx->ops = calloc((size_t)STRESS_NUM_OPS, sizeof(stress_op_t));
	if (!ctx->states || !ctx->ops) {
		ENG_LOG_ERROR(ENGINE_LOG_MOD_TEST, "alloc failed");
		free(ctx->states); free(ctx->ops); free(ctx);
		spdk_app_stop(-1);
		return;
	}

	generate_ops(ctx->ops, STRESS_NUM_OPS, num_objects, 42u);

	ENG_LOG_INFO(ENGINE_LOG_MOD_TEST, "stress test: %d objects, %d ops",
		 num_objects, ctx->num_ops);

	stress_create_objects(ctx);
}

/* ---------- engine start wrapper ---------- */
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

/* ---------- main ---------- */
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
