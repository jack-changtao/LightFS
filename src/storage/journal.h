#ifndef LIGHTFS_JOURNAL_H
#define LIGHTFS_JOURNAL_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"
#include "segment.h"
#include <stdint.h>

typedef enum {
    JOURNAL_PUT = 0,
    JOURNAL_DELETE,
    JOURNAL_SEAL,
} journal_op_t;

typedef struct journal_record {
    journal_op_t op;
    uint64_t seq;
    blob_id_t blob_id;
    blob_location_t location;
    uint32_t crc;
} journal_record_t;

typedef struct journal {
    segment_t *segment;
    uint64_t write_seq;
    uint64_t bytes_written;
    uint64_t capacity;
} journal_t;

journal_t *journal_init(segment_manager_t *mgr);
void journal_destroy(journal_t *j);

int journal_append_put(journal_t *j, blob_id_t id, const blob_location_t *loc);
int journal_append_delete(journal_t *j, blob_id_t id);
int journal_seal(journal_t *j);

typedef int (*journal_replay_cb)(journal_op_t op, blob_id_t id,
                                  const blob_location_t *loc, void *arg);
int journal_replay(journal_t *j, journal_replay_cb cb, void *arg);

#endif /* LIGHTFS_JOURNAL_H */
