#include "bs_internal.h"
#include <stdlib.h>
#include <string.h>

static bs_context_t *g_bs = NULL;

int bs_init(const bs_config_t *cfg) {
    if (!cfg || g_bs) return -1;

    g_bs = calloc(1, sizeof(bs_context_t));
    if (!g_bs) return -1;

    g_bs->segment_size = cfg->segment_size;
    g_bs->seg_mgr = segment_manager_init(cfg->segment_size);
    if (!g_bs->seg_mgr) {
        free(g_bs);
        g_bs = NULL;
        return -1;
    }

    g_bs->index = cow_btree_create();
    if (!g_bs->index) {
        segment_manager_destroy(g_bs->seg_mgr);
        free(g_bs);
        g_bs = NULL;
        return -1;
    }

    g_bs->journal = journal_init(g_bs->seg_mgr);
    if (!g_bs->journal) {
        cow_btree_destroy(g_bs->index);
        segment_manager_destroy(g_bs->seg_mgr);
        free(g_bs);
        g_bs = NULL;
        return -1;
    }

    return 0;
}

void bs_destroy(void) {
    if (!g_bs) return;

    if (g_bs->journal) journal_destroy(g_bs->journal);
    if (g_bs->index) cow_btree_destroy(g_bs->index);
    if (g_bs->seg_mgr) segment_manager_destroy(g_bs->seg_mgr);
    free(g_bs);
    g_bs = NULL;
}

int bs_alloc_location(blob_location_t *loc) {
    if (!g_bs || !loc) return -1;

    segment_t *seg = segment_alloc(g_bs->seg_mgr, SEG_TYPE_DATA);
    if (!seg) return -1;

    loc->segment_id = seg->id;
    loc->offset = seg->used;
    loc->size = 0;
    loc->crc = 0;
    return 0;
}

int bs_write_to_segment(segment_t *seg, const void *data, uint32_t size,
                         uint32_t *offset_out) {
    if (!seg || !data || !offset_out) return -1;
    if (seg->used + size > seg->size) return -1;

    *offset_out = seg->used;
    seg->used += size;
    return 0;
}

int bs_read_from_segment(const segment_t *seg, uint32_t offset,
                          void *data, uint32_t size) {
    (void)seg;
    (void)data;
    (void)offset;
    (void)size;
    return 0;
}

static void put_callback(int rc, const blob_location_t *loc, void *arg) {
    (void)rc;
    (void)loc;
    (void)arg;
}

int bs_put_blob(blob_id_t id, const void *data, uint32_t size,
                bs_put_cb cb, void *arg) {
    if (!g_bs || !data) return -1;
    if (!cb) cb = put_callback;

    blob_location_t loc;
    if (bs_alloc_location(&loc) < 0) {
        cb(-1, NULL, arg);
        return -1;
    }

    loc.size = size;
    loc.crc = 0;

    if (journal_append_put(g_bs->journal, id, &loc) < 0) {
        cb(-1, NULL, arg);
        return -1;
    }

    cow_btree_insert(g_bs->index, id, &loc);
    g_bs->dirty = 1;

    cb(0, &loc, arg);
    return 0;
}

static void get_callback(int rc, const void *data, uint32_t size, void *arg) {
    (void)rc;
    (void)data;
    (void)size;
    (void)arg;
}

int bs_get_blob(const blob_location_t *loc, bs_get_cb cb, void *arg) {
    if (!g_bs || !loc) return -1;
    if (!cb) cb = get_callback;

    if (loc->size == 0) {
        cb(-1, NULL, 0, arg);
        return -1;
    }

    cb(0, NULL, loc->size, arg);
    return 0;
}

static void delete_callback(int rc, void *arg) {
    (void)rc;
    (void)arg;
}

int bs_delete_blob(blob_id_t id, bs_delete_cb cb, void *arg) {
    if (!g_bs) return -1;
    if (!cb) cb = delete_callback;

    blob_location_t loc;
    if (cow_btree_lookup(g_bs->index, id, &loc) < 0) {
        cb(-1, arg);
        return -1;
    }

    if (journal_append_delete(g_bs->journal, id) < 0) {
        cb(-1, arg);
        return -1;
    }

    cow_btree_delete(g_bs->index, id);
    g_bs->dirty = 1;

    cb(0, arg);
    return 0;
}

int bs_stat_blob(blob_id_t id, blob_state_t *state_out) {
    if (!g_bs || !state_out) return -1;

    blob_location_t loc;
    if (cow_btree_lookup(g_bs->index, id, &loc) < 0) {
        *state_out = BLOB_STATE_FREE;
        return 0;
    }

    *state_out = BLOB_STATE_ACTIVE;
    return 0;
}

int bs_gc_run(void) {
    if (!g_bs) return -1;
    return 0;
}
