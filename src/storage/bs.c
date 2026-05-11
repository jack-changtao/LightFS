#include "bs_internal.h"
#include <stdlib.h>
#include <string.h>

static bs_context_t *g_blob_store = NULL;

int bs_init(const bs_config_t *config) {
    if (!config || g_blob_store) return -1;

    g_blob_store = calloc(1, sizeof(bs_context_t));
    if (!g_blob_store) return -1;

    g_blob_store->segment_size = config->segment_size;
    g_blob_store->segment_manager = segment_manager_initialize(config->segment_size);
    if (!g_blob_store->segment_manager) {
        free(g_blob_store);
        g_blob_store = NULL;
        return -1;
    }

    g_blob_store->index = cow_btree_create();
    if (!g_blob_store->index) {
        segment_manager_destroy(g_blob_store->segment_manager);
        free(g_blob_store);
        g_blob_store = NULL;
        return -1;
    }

    g_blob_store->journal = journal_init(g_blob_store->segment_manager);
    if (!g_blob_store->journal) {
        cow_btree_destroy(g_blob_store->index);
        segment_manager_destroy(g_blob_store->segment_manager);
        free(g_blob_store);
        g_blob_store = NULL;
        return -1;
    }

    return 0;
}

void bs_destroy(void) {
    if (!g_blob_store) return;

    if (g_blob_store->journal) journal_destroy(g_blob_store->journal);
    if (g_blob_store->index) cow_btree_destroy(g_blob_store->index);
    if (g_blob_store->segment_manager) segment_manager_destroy(g_blob_store->segment_manager);
    free(g_blob_store);
    g_blob_store = NULL;
}

int bs_allocate_location(blob_location_t *location) {
    if (!g_blob_store || !location) return -1;

    segment_t *segment = segment_allocate(g_blob_store->segment_manager, SEGMENT_TYPE_DATA);
    if (!segment) return -1;

    location->segment_id = segment->id;
    location->offset = segment->used;
    location->size = 0;
    location->checksum = 0;
    return 0;
}

int bs_write_to_segment(segment_t *segment, const void *data, uint32_t size,
                         uint32_t *offset_out) {
    if (!segment || !data || !offset_out) return -1;
    if (segment->used + size > segment->size) return -1;

    *offset_out = segment->used;
    segment->used += size;
    return 0;
}

int bs_read_from_segment(const segment_t *segment, uint32_t offset,
                          void *data, uint32_t size) {
    (void)segment;
    (void)data;
    (void)offset;
    (void)size;
    return 0;
}

static void put_callback(int result, const blob_location_t *location, void *user_data) {
    (void)result;
    (void)location;
    (void)user_data;
}

int bs_put_blob(blob_id_t id, const void *data, uint32_t size,
                bs_put_callback callback, void *user_data) {
    if (!g_blob_store || !data) return -1;
    if (!callback) callback = put_callback;

    blob_location_t location;
    if (bs_allocate_location(&location) < 0) {
        callback(-1, NULL, user_data);
        return -1;
    }

    location.size = size;
    location.checksum = 0;

    if (journal_append_put(g_blob_store->journal, id, &location) < 0) {
        callback(-1, NULL, user_data);
        return -1;
    }

    cow_btree_insert(g_blob_store->index, id, &location);
    g_blob_store->is_dirty = 1;

    callback(0, &location, user_data);
    return 0;
}

static void get_callback(int result, const void *data, uint32_t size, void *user_data) {
    (void)result;
    (void)data;
    (void)size;
    (void)user_data;
}

int bs_get_blob(const blob_location_t *location, bs_get_callback callback, void *user_data) {
    if (!g_blob_store || !location) return -1;
    if (!callback) callback = get_callback;

    if (location->size == 0) {
        callback(-1, NULL, 0, user_data);
        return -1;
    }

    callback(0, NULL, location->size, user_data);
    return 0;
}

static void delete_callback(int result, void *user_data) {
    (void)result;
    (void)user_data;
}

int bs_delete_blob(blob_id_t id, bs_delete_callback callback, void *user_data) {
    if (!g_blob_store) return -1;
    if (!callback) callback = delete_callback;

    blob_location_t location;
    if (cow_btree_lookup(g_blob_store->index, id, &location) < 0) {
        callback(-1, user_data);
        return -1;
    }

    if (journal_append_delete(g_blob_store->journal, id) < 0) {
        callback(-1, user_data);
        return -1;
    }

    cow_btree_delete(g_blob_store->index, id);
    g_blob_store->is_dirty = 1;

    callback(0, user_data);
    return 0;
}

int bs_stat_blob(blob_id_t id, blob_state_t *state_out) {
    if (!g_blob_store || !state_out) return -1;

    blob_location_t location;
    if (cow_btree_lookup(g_blob_store->index, id, &location) < 0) {
        *state_out = BLOB_STATE_FREE;
        return 0;
    }

    *state_out = BLOB_STATE_ACTIVE;
    return 0;
}

int bs_garbage_collection_run(void) {
    if (!g_blob_store) return -1;
    return 0;
}
