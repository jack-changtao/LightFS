#include "journal.h"
#include <stdlib.h>
#include <string.h>

static uint32_t journal_checksum32(const journal_record_t *record) {
    uint32_t checksum = 0;
    checksum ^= (uint32_t)record->operation;
    checksum ^= (uint32_t)(record->sequence & 0xFFFFFFFF);
    checksum ^= (uint32_t)(record->blob_id & 0xFFFFFFFF);
    checksum ^= (uint32_t)(record->blob_id >> 32);
    checksum ^= record->location.segment_id;
    checksum ^= record->location.checksum;
    return checksum;
}

journal_t *journal_init(segment_manager_t *manager) {
    if (!manager) return NULL;

    segment_t *segment = segment_allocate(manager, SEGMENT_TYPE_JOURNAL);
    if (!segment) return NULL;

    journal_t *journal = calloc(1, sizeof(journal_t));
    if (!journal) {
        segment_free(segment);
        return NULL;
    }

    journal->segment = segment;
    journal->write_sequence = 0;
    journal->bytes_written = 0;
    journal->capacity = segment->size;
    return journal;
}

void journal_destroy(journal_t *journal) {
    if (!journal) return;
    if (journal->segment) {
        segment_free(journal->segment);
    }
    free(journal);
}

static int journal_append_record(journal_t *journal, journal_record_t *record) {
    if (!journal || !journal->segment || journal->segment->state == SEGMENT_SEALED)
        return -1;

    if (journal->bytes_written + sizeof(journal_record_t) > journal->capacity) {
        return -1;
    }

    record->sequence = ++journal->write_sequence;
    record->checksum = journal_checksum32(record);

    journal->bytes_written += sizeof(journal_record_t);
    journal->segment->used = journal->bytes_written;
    return 0;
}

int journal_append_put(journal_t *journal, blob_id_t id, const blob_location_t *location) {
    if (!journal || !location) return -1;

    journal_record_t record = {
        .operation = JOURNAL_OPERATION_PUT,
        .blob_id = id,
        .location = *location,
    };
    return journal_append_record(journal, &record);
}

int journal_append_delete(journal_t *journal, blob_id_t id) {
    if (!journal) return -1;

    journal_record_t record = {
        .operation = JOURNAL_OPERATION_DELETE,
        .blob_id = id,
    };
    return journal_append_record(journal, &record);
}

int journal_seal(journal_t *journal) {
    if (!journal || !journal->segment) return -1;
    segment_seal(journal->segment);
    return 0;
}

int journal_replay(journal_t *journal, journal_replay_callback callback, void *argument) {
    if (!journal || !callback) return -1;
    return 0;
}
