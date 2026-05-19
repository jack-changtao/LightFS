#ifndef LIGHTFS_GC_H
#define LIGHTFS_GC_H

#include "segment.h"
#include <stdint.h>

typedef struct {
  segment_manager_t *manager;
  uint32_t threshold;
  uint64_t last_run;
} garbage_collection_context_t;

int garbage_collection_initialize(garbage_collection_context_t *context, segment_manager_t *manager);
void garbage_collection_destroy(garbage_collection_context_t *context);
int garbage_collection_should_run(garbage_collection_context_t *context);
int garbage_collection_run(garbage_collection_context_t *context);

#endif /* LIGHTFS_GC_H */
