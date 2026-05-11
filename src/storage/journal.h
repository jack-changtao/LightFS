#ifndef LIGHTFS_JOURNAL_H
#define LIGHTFS_JOURNAL_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"
#include "segment.h"
#include <stdint.h>

typedef enum {
    JOURNAL_OPERATION_PUT = 0,
    JOURNAL_OPERATION_DELETE,
    JOURNAL_OPERATION_SEAL,
} journal_operation_t;

typedef struct journal_record {
    journal_operation_t operation;
    uint64_t sequence;
    blob_id_t blob_id;
    blob_location_t location;
    uint32_t checksum;
} journal_record_t;

typedef struct journal {
    segment_t *segment;
    uint64_t write_sequence;
    uint64_t bytes_written;
    uint64_t capacity;
} journal_t;

journal_t *journal_init(segment_manager_t *manager);
void journal_destroy(journal_t *journal);

int journal_append_put(journal_t *journal, blob_id_t id, const blob_location_t *location);
int journal_append_delete(journal_t *journal, blob_id_t id);
int journal_seal(journal_t *journal);

typedef int (*journal_replay_callback)(journal_operation_t operation, blob_id_t id,
                                  const blob_location_t *location, void *argument);
int journal_replay(journal_t *journal, journal_replay_callback callback, void *argument);

#endif /* LIGHTFS_JOURNAL_H */
