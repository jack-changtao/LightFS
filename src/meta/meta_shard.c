#include "lightfs/meta/meta_shard.h"
#include "lightfs/meta/meta_types.h"
#include "lightfs/bs_cow_btree.h"
#include <stdlib.h>
#include <string.h>

struct meta_shard {
    uint32_t shard_id;
    uint32_t parent_shard_id;
    cow_btree_t *btree;
    char bucket_name[META_MAX_BUCKET_LEN + 1];
    char key_min[META_MAX_KEY_LEN + 1];
    char key_max[META_MAX_KEY_LEN + 1];
    int has_loading_child;
    object_manifest_t *entries;
    int entry_count;
    int entry_capacity;
};

#define SHARD_DEFAULT_CAPACITY 1024

meta_shard_t *meta_shard_create(uint32_t shard_id,
                                 uint32_t parent_shard_id,
                                 const char *bucket_name) {
    meta_shard_t *shard = calloc(1, sizeof(meta_shard_t));
    if (!shard) return NULL;

    shard->shard_id = shard_id;
    shard->parent_shard_id = parent_shard_id;
    if (bucket_name) {
        strncpy(shard->bucket_name, bucket_name, sizeof(shard->bucket_name) - 1);
    }
    shard->key_min[0] = '\0';
    shard->key_max[0] = '\0';

    shard->entry_capacity = SHARD_DEFAULT_CAPACITY;
    shard->entries = calloc(shard->entry_capacity, sizeof(object_manifest_t));
    if (!shard->entries) {
        free(shard);
        return NULL;
    }

    return shard;
}

void meta_shard_destroy(meta_shard_t *shard) {
    if (!shard) return;
    for (int i = 0; i < shard->entry_count; i++) {
        if (shard->entries[i].fragments) {
            free(shard->entries[i].fragments);
        }
    }
    free(shard->entries);
    if (shard->btree) cow_btree_destroy(shard->btree);
    free(shard);
}

static int find_entry(meta_shard_t *shard, const char *bucket, const char *key) {
    for (int i = 0; i < shard->entry_count; i++) {
        if (strcmp(shard->entries[i].bucket, bucket) == 0 &&
            strcmp(shard->entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int meta_shard_insert(meta_shard_t *shard, const object_manifest_t *manifest) {
    if (!shard || !manifest) return -1;

    int idx = find_entry(shard, manifest->bucket, manifest->key);
    if (idx >= 0) {
        shard->entries[idx] = *manifest;
        if (manifest->fragments && manifest->fragment_count > 0) {
            shard->entries[idx].fragments = malloc(
                manifest->fragment_count * sizeof(blob_location_t));
            if (shard->entries[idx].fragments) {
                memcpy(shard->entries[idx].fragments, manifest->fragments,
                       manifest->fragment_count * sizeof(blob_location_t));
            }
        }
        return 0;
    }

    if (shard->entry_count >= shard->entry_capacity) {
        int new_cap = shard->entry_capacity * 2;
        object_manifest_t *new_entries = realloc(shard->entries,
                                                  new_cap * sizeof(object_manifest_t));
        if (!new_entries) return -1;
        shard->entries = new_entries;
        shard->entry_capacity = new_cap;
    }

    shard->entries[shard->entry_count++] = *manifest;

    if (manifest->fragments && manifest->fragment_count > 0) {
        shard->entries[shard->entry_count - 1].fragments =
            malloc(manifest->fragment_count * sizeof(blob_location_t));
        if (shard->entries[shard->entry_count - 1].fragments) {
            memcpy(shard->entries[shard->entry_count - 1].fragments,
                   manifest->fragments,
                   manifest->fragment_count * sizeof(blob_location_t));
        }
    }

    return 0;
}

int meta_shard_lookup(meta_shard_t *shard,
                       const char *bucket, const char *key,
                       object_manifest_t *out) {
    if (!shard || !bucket || !key || !out) return -1;

    int idx = find_entry(shard, bucket, key);
    if (idx < 0) return -1;

    *out = shard->entries[idx];
    return 0;
}

int meta_shard_delete(meta_shard_t *shard,
                       const char *bucket, const char *key) {
    if (!shard || !bucket || !key) return -1;

    int idx = find_entry(shard, bucket, key);
    if (idx < 0) return -1;

    if (shard->entries[idx].fragments) {
        free(shard->entries[idx].fragments);
    }

    for (int i = idx; i < shard->entry_count - 1; i++) {
        shard->entries[i] = shard->entries[i + 1];
    }
    shard->entry_count--;
    return 0;
}

int meta_shard_list(meta_shard_t *shard,
                     const char *bucket,
                     const char *prefix,
                     const char *marker,
                     int max_keys,
                     char **keys_out,
                     int *count_out) {
    if (!shard || !bucket || !keys_out || !count_out) return -1;

    int prefix_len = prefix ? (int)strlen(prefix) : 0;
    int start = 0;

    if (marker && marker[0]) {
        for (int i = 0; i < shard->entry_count; i++) {
            if (strcmp(shard->entries[i].key, marker) > 0) {
                start = i;
                break;
            }
        }
    }

    int count = 0;
    for (int i = start; i < shard->entry_count && count < max_keys; i++) {
        if (strcmp(shard->entries[i].bucket, bucket) != 0) continue;

        if (prefix_len > 0 &&
            strncmp(shard->entries[i].key, prefix, prefix_len) != 0) continue;

        strncpy(keys_out[count], shard->entries[i].key, 63);
        keys_out[count][63] = '\0';
        count++;
    }

    *count_out = count;
    return 0;
}

int meta_shard_count(meta_shard_t *shard) {
    return shard ? shard->entry_count : 0;
}

meta_shard_t *meta_shard_split(meta_shard_t *shard, uint32_t new_shard_id) {
    if (!shard || shard->has_loading_child) return NULL;

    int mid = shard->entry_count / 2;
    if (mid <= 0) return NULL;

    meta_shard_t *child = meta_shard_create(new_shard_id, shard->shard_id,
                                             shard->bucket_name);
    if (!child) return NULL;

    child->has_loading_child = 1;

    shard->has_loading_child = 1;
    return child;
}

int meta_shard_has_loading_child(meta_shard_t *shard) {
    return shard ? shard->has_loading_child : 0;
}

uint32_t meta_shard_get_id(meta_shard_t *shard) {
    return shard ? shard->shard_id : 0;
}

int meta_shard_get_count(meta_shard_t *shard) {
    return shard ? shard->entry_count : 0;
}

object_manifest_t *meta_shard_get_entries(meta_shard_t *shard) {
    return shard ? shard->entries : NULL;
}

void meta_shard_child_activated(meta_shard_t *shard) {
    if (shard) shard->has_loading_child = 0;
}
