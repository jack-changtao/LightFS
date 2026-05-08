#ifndef LIGHTFS_BS_INTERNAL_H
#define LIGHTFS_BS_INTERNAL_H

#include "lightfs/bs.h"
#include "segment.h"
#include "journal.h"
#include "cow_btree.h"

typedef struct {
    segment_manager_t *seg_mgr;
    cow_btree_t *index;
    journal_t *journal;
    uint32_t segment_size;
    int dirty;
} bs_context_t;

int bs_alloc_location(blob_location_t *loc);
int bs_write_to_segment(segment_t *seg, const void *data, uint32_t size,
                         uint32_t *offset_out);
int bs_read_from_segment(const segment_t *seg, uint32_t offset,
                          void *data, uint32_t size);

#endif /* LIGHTFS_BS_INTERNAL_H */
