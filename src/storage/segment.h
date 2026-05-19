#ifndef LIGHTFS_SEGMENT_H
#define LIGHTFS_SEGMENT_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"
#include <stdint.h>

typedef enum {
  SEGMENT_FREE = 0,
  SEGMENT_ACTIVE,
  SEGMENT_SEALED,
  SEGMENT_CLEANING,
} segment_state_t;

typedef enum {
  SEGMENT_TYPE_DATA = 0,
  SEGMENT_TYPE_METADATA,
  SEGMENT_TYPE_JOURNAL,
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

segment_manager_t *segment_manager_initialize(uint64_t segment_size);
void segment_manager_destroy(segment_manager_t *manager);

segment_t *segment_allocate(segment_manager_t *manager, segment_type_t type);
void segment_seal(segment_t *segment);
void segment_start_cleaning(segment_t *segment);
void segment_free(segment_t *segment);

segment_t *segment_find_garbage_collection_victim(segment_manager_t *manager,
                 uint32_t liveness_threshold,
                 segment_type_t type);

#endif /* LIGHTFS_SEGMENT_H */
