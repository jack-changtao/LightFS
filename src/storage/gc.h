#ifndef LIGHTFS_GC_H
#define LIGHTFS_GC_H

#include "segment.h"
#include <stdint.h>

typedef struct {
    segment_manager_t *mgr;
    uint32_t threshold;
    uint64_t last_run;
} gc_context_t;

int gc_init(gc_context_t *ctx, segment_manager_t *mgr);
void gc_destroy(gc_context_t *ctx);
int gc_should_run(gc_context_t *ctx);
int gc_run(gc_context_t *ctx);

#endif /* LIGHTFS_GC_H */
