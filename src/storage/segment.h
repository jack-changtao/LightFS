#ifndef LIGHTFS_SEGMENT_H
#define LIGHTFS_SEGMENT_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"
#include <stdint.h>

typedef enum {
    SEG_FREE = 0,
    SEG_ACTIVE,
    SEG_SEALED,
    SEG_CLEANING,
} segment_state_t;

typedef enum {
    SEG_TYPE_DATA = 0,
    SEG_TYPE_META,
    SEG_TYPE_JOURNAL,
} segment_type_t;

typedef struct segment {
    segment_id_t id;
    segment_state_t state;
    segment_type_t type;
    uint64_t offset;
    uint64_t size;
    uint64_t used;
    uint64_t live_bytes;
} segment_t;

typedef struct segment_manager {
    segment_t **segments;
    uint32_t capacity;
    uint32_t count;
    uint64_t segment_size;
} segment_manager_t;

segment_manager_t *segment_manager_init(uint64_t segment_size);
void segment_manager_destroy(segment_manager_t *mgr);

segment_t *segment_alloc(segment_manager_t *mgr, segment_type_t type);
void segment_seal(segment_t *seg);
void segment_start_cleaning(segment_t *seg);
void segment_free(segment_t *seg);

segment_t *segment_find_gc_victim(segment_manager_t *mgr,
                                   uint32_t liveness_threshold,
                                   segment_type_t type);

#endif /* LIGHTFS_SEGMENT_H */
