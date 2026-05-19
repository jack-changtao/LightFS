#include "gc.h"
#include "segment.h"
#include <stdlib.h>

int garbage_collection_initialize(garbage_collection_context_t *context, segment_manager_t *manager) {
  if (!context || !manager) return -1;

  context->manager = manager;
  context->threshold = 20;
  context->last_run = 0;
  return 0;
}

void garbage_collection_destroy(garbage_collection_context_t *context) {
  (void)context;
}

int garbage_collection_should_run(garbage_collection_context_t *context) {
  if (!context) return 0;

  for (uint32_t i = 0; i < context->manager->count; i++) {
    segment_t *segment = context->manager->segments[i];
    if (!segment || segment->state != SEGMENT_SEALED) continue;

    int liveness = 0;
    if (segment->size > 0) {
      liveness = (int)((segment->live_bytes * 100) / segment->size);
    }

    if (liveness < (int)context->threshold) {
      return 1;
    }
  }
  return 0;
}

int garbage_collection_run(garbage_collection_context_t *context) {
  if (!context) return -1;

  segment_t *victim = segment_find_garbage_collection_victim(context->manager, context->threshold, SEGMENT_TYPE_DATA);
  if (!victim) return 0;

  segment_start_cleaning(victim);
  victim->live_bytes = 0;
  segment_free(victim);

  return 0;
}
