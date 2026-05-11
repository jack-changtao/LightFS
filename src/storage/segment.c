#include "segment.h"
#include <stdlib.h>
#include <string.h>

#define SEGMENT_DEFAULT_CAPACITY 1024

static uint32_t next_identifier = 1;

static segment_t *segment_new(segment_type_t type, uint64_t size) {
    segment_t *segment = calloc(1, sizeof(segment_t));
    if (!segment) return NULL;
    segment->id = next_identifier++;
    segment->state = SEGMENT_ACTIVE;
    segment->type = type;
    segment->size = size;
    segment->used = 0;
    segment->live_bytes = 0;
    return segment;
}

static int liveness_percent(segment_t *segment) {
    if (segment->size == 0) return 0;
    return (int)((segment->live_bytes * 100) / segment->size);
}

segment_manager_t *segment_manager_initialize(uint64_t segment_size) {
    segment_manager_t *manager = calloc(1, sizeof(segment_manager_t));
    if (!manager) return NULL;

    manager->segment_size = segment_size;
    manager->capacity = SEGMENT_DEFAULT_CAPACITY;
    manager->count = 0;
    manager->segments = calloc(manager->capacity, sizeof(segment_t *));
    if (!manager->segments) {
        free(manager);
        return NULL;
    }
    return manager;
}

void segment_manager_destroy(segment_manager_t *manager) {
    if (!manager) return;
    for (uint32_t i = 0; i < manager->capacity; i++) {
        if (manager->segments[i]) {
            free(manager->segments[i]);
        }
    }
    free(manager->segments);
    free(manager);
}

segment_t *segment_allocate(segment_manager_t *manager, segment_type_t type) {
    if (!manager) return NULL;

    for (uint32_t i = 0; i < manager->capacity; i++) {
        if (manager->segments[i] == NULL || manager->segments[i]->state == SEGMENT_FREE) {
            segment_t *segment = segment_new(type, manager->segment_size);
            if (!segment) return NULL;
            manager->segments[i] = segment;
            manager->count++;
            return segment;
        }
    }
    return NULL;
}

void segment_seal(segment_t *segment) {
    if (!segment || segment->state != SEGMENT_ACTIVE) return;
    segment->state = SEGMENT_SEALED;
}

void segment_start_cleaning(segment_t *segment) {
    if (!segment || segment->state != SEGMENT_SEALED) return;
    segment->state = SEGMENT_CLEANING;
}

void segment_free(segment_t *segment) {
    if (!segment) return;
    segment->state = SEGMENT_FREE;
    segment->used = 0;
    segment->live_bytes = 0;
}

segment_t *segment_find_garbage_collection_victim(segment_manager_t *manager,
                                   uint32_t liveness_threshold,
                                   segment_type_t type) {
    if (!manager) return NULL;

    segment_t *best = NULL;
    int best_percent = 101;

    for (uint32_t i = 0; i < manager->capacity; i++) {
        segment_t *segment = manager->segments[i];
        if (!segment || segment->state != SEGMENT_SEALED || segment->type != type)
            continue;

        int percent = liveness_percent(segment);
        if (percent < (int)liveness_threshold && percent < best_percent) {
            best = segment;
            best_percent = percent;
        }
    }
    return best;
}
