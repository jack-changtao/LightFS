#ifndef LIGHTFS_BS_INTERNAL_H
#define LIGHTFS_BS_INTERNAL_H

#include "lightfs/bs.h"
#include "segment.h"
#include "journal.h"
#include "cow_btree.h"

typedef struct {
    segment_manager_t *segment_manager;
    cow_btree_t *index;
    journal_t *journal;
    uint32_t segment_size;
    int is_dirty;
} bs_context_t;

int bs_allocate_location(blob_location_t *location);
int bs_write_to_segment(segment_t *segment, const void *data, uint32_t size,
                         uint32_t *offset_out);
int bs_read_from_segment(const segment_t *segment, uint32_t offset,
                          void *data, uint32_t size);

#endif /* LIGHTFS_BS_INTERNAL_H */
