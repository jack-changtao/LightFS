#include "gc.h"
#include "segment.h"
#include <stdlib.h>

int gc_init(gc_context_t *ctx, segment_manager_t *mgr) {
    if (!ctx || !mgr) return -1;

    ctx->mgr = mgr;
    ctx->threshold = 20;
    ctx->last_run = 0;
    return 0;
}

void gc_destroy(gc_context_t *ctx) {
    (void)ctx;
}

int gc_should_run(gc_context_t *ctx) {
    if (!ctx) return 0;

    for (uint32_t i = 0; i < ctx->mgr->count; i++) {
        segment_t *seg = ctx->mgr->segments[i];
        if (!seg || seg->state != SEG_SEALED) continue;

        int liveness = 0;
        if (seg->size > 0) {
            liveness = (int)((seg->live_bytes * 100) / seg->size);
        }

        if (liveness < (int)ctx->threshold) {
            return 1;
        }
    }
    return 0;
}

int gc_run(gc_context_t *ctx) {
    if (!ctx) return -1;

    segment_t *victim = segment_find_gc_victim(ctx->mgr, ctx->threshold, SEG_TYPE_DATA);
    if (!victim) return 0;

    segment_start_cleaning(victim);
    victim->live_bytes = 0;
    segment_free(victim);

    return 0;
}
