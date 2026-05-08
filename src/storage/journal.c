#include "journal.h"
#include <stdlib.h>
#include <string.h>

static uint32_t journal_crc32(const journal_record_t *rec) {
    uint32_t crc = 0;
    crc ^= (uint32_t)rec->op;
    crc ^= (uint32_t)(rec->seq & 0xFFFFFFFF);
    crc ^= (uint32_t)(rec->blob_id & 0xFFFFFFFF);
    crc ^= (uint32_t)(rec->blob_id >> 32);
    crc ^= rec->location.segment_id;
    crc ^= rec->location.crc;
    return crc;
}

journal_t *journal_init(segment_manager_t *mgr) {
    if (!mgr) return NULL;

    segment_t *seg = segment_alloc(mgr, SEG_TYPE_JOURNAL);
    if (!seg) return NULL;

    journal_t *j = calloc(1, sizeof(journal_t));
    if (!j) {
        segment_free(seg);
        return NULL;
    }

    j->segment = seg;
    j->write_seq = 0;
    j->bytes_written = 0;
    j->capacity = seg->size;
    return j;
}

void journal_destroy(journal_t *j) {
    if (!j) return;
    if (j->segment) {
        segment_free(j->segment);
    }
    free(j);
}

static int journal_append_record(journal_t *j, journal_record_t *rec) {
    if (!j || !j->segment || j->segment->state == SEG_SEALED)
        return -1;

    if (j->bytes_written + sizeof(journal_record_t) > j->capacity) {
        return -1;
    }

    rec->seq = ++j->write_seq;
    rec->crc = journal_crc32(rec);

    j->bytes_written += sizeof(journal_record_t);
    j->segment->used = j->bytes_written;
    return 0;
}

int journal_append_put(journal_t *j, blob_id_t id, const blob_location_t *loc) {
    if (!j || !loc) return -1;

    journal_record_t rec = {
        .op = JOURNAL_PUT,
        .blob_id = id,
        .location = *loc,
    };
    return journal_append_record(j, &rec);
}

int journal_append_delete(journal_t *j, blob_id_t id) {
    if (!j) return -1;

    journal_record_t rec = {
        .op = JOURNAL_DELETE,
        .blob_id = id,
    };
    return journal_append_record(j, &rec);
}

int journal_seal(journal_t *j) {
    if (!j || !j->segment) return -1;
    segment_seal(j->segment);
    return 0;
}

int journal_replay(journal_t *j, journal_replay_cb cb, void *arg) {
    if (!j || !cb) return -1;
    return 0;
}
