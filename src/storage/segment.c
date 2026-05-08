#include "segment.h"
#include <stdlib.h>
#include <string.h>

#define SEGMENT_DEFAULT_CAPACITY 1024

static uint32_t g_next_id = 1;

static segment_t *segment_new(segment_type_t type, uint64_t size) {
    segment_t *seg = calloc(1, sizeof(segment_t));
    if (!seg) return NULL;
    seg->id = g_next_id++;
    seg->state = SEG_ACTIVE;
    seg->type = type;
    seg->size = size;
    seg->used = 0;
    seg->live_bytes = 0;
    return seg;
}

static int liveness_pct(segment_t *seg) {
    if (seg->size == 0) return 0;
    return (int)((seg->live_bytes * 100) / seg->size);
}

segment_manager_t *segment_manager_init(uint64_t segment_size) {
    segment_manager_t *mgr = calloc(1, sizeof(segment_manager_t));
    if (!mgr) return NULL;

    mgr->segment_size = segment_size;
    mgr->capacity = SEGMENT_DEFAULT_CAPACITY;
    mgr->count = 0;
    mgr->segments = calloc(mgr->capacity, sizeof(segment_t *));
    if (!mgr->segments) {
        free(mgr);
        return NULL;
    }
    return mgr;
}

void segment_manager_destroy(segment_manager_t *mgr) {
    if (!mgr) return;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        if (mgr->segments[i]) {
            free(mgr->segments[i]);
        }
    }
    free(mgr->segments);
    free(mgr);
}

segment_t *segment_alloc(segment_manager_t *mgr, segment_type_t type) {
    if (!mgr) return NULL;

    for (uint32_t i = 0; i < mgr->capacity; i++) {
        if (mgr->segments[i] == NULL || mgr->segments[i]->state == SEG_FREE) {
            segment_t *seg = segment_new(type, mgr->segment_size);
            if (!seg) return NULL;
            mgr->segments[i] = seg;
            mgr->count++;
            return seg;
        }
    }
    return NULL;
}

void segment_seal(segment_t *seg) {
    if (!seg || seg->state != SEG_ACTIVE) return;
    seg->state = SEG_SEALED;
}

void segment_start_cleaning(segment_t *seg) {
    if (!seg || seg->state != SEG_SEALED) return;
    seg->state = SEG_CLEANING;
}

void segment_free(segment_t *seg) {
    if (!seg) return;
    seg->state = SEG_FREE;
    seg->used = 0;
    seg->live_bytes = 0;
}

segment_t *segment_find_gc_victim(segment_manager_t *mgr,
                                   uint32_t liveness_threshold,
                                   segment_type_t type) {
    if (!mgr) return NULL;

    segment_t *best = NULL;
    int best_pct = 101;

    for (uint32_t i = 0; i < mgr->capacity; i++) {
        segment_t *seg = mgr->segments[i];
        if (!seg || seg->state != SEG_SEALED || seg->type != type)
            continue;

        int pct = liveness_pct(seg);
        if (pct < (int)liveness_threshold && pct < best_pct) {
            best = seg;
            best_pct = pct;
        }
    }
    return best;
}
