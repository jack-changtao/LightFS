#include "gateway_manifest_cache.h"
#include <stdlib.h>
#include <string.h>

typedef struct cache_entry {
    char bucket[META_MAX_BUCKET_LEN + 1];
    char key[META_MAX_KEY_LEN + 1];
    object_manifest_t manifest;
    int valid;
} cache_entry_t;

struct manifest_cache {
    cache_entry_t *entries;
    int capacity;
    int count;
};

manifest_cache_t *manifest_cache_create(int max_entries) {
    manifest_cache_t *c = calloc(1, sizeof(manifest_cache_t));
    if (!c) return NULL;

    c->capacity = max_entries;
    c->entries = calloc(max_entries, sizeof(cache_entry_t));
    if (!c->entries) {
        free(c);
        return NULL;
    }
    return c;
}

void manifest_cache_destroy(manifest_cache_t *cache) {
    if (!cache) return;
    free(cache->entries);
    free(cache);
}

static int make_index(const char *bucket, const char *key, int capacity) {
    int h = 0;
    while (*bucket) h = (h * 31 + *bucket++) & 0x7FFFFFFF;
    while (*key) h = (h * 31 + *key++) & 0x7FFFFFFF;
    return h % capacity;
}

int manifest_cache_lookup(manifest_cache_t *cache,
                           const char *bucket, const char *key,
                           object_manifest_t *out) {
    if (!cache || !bucket || !key || !out) return -1;

    int idx = make_index(bucket, key, cache->capacity);

    for (int i = 0; i < cache->capacity; i++) {
        int probe = (idx + i) % cache->capacity;
        cache_entry_t *e = &cache->entries[probe];
        if (!e->valid) return -1;
        if (strcmp(e->bucket, bucket) == 0 &&
            strcmp(e->key, key) == 0) {
            *out = e->manifest;
            return 0;
        }
    }
    return -1;
}

void manifest_cache_insert(manifest_cache_t *cache,
                            const char *bucket, const char *key,
                            const object_manifest_t *manifest) {
    if (!cache || !bucket || !key || !manifest) return;

    int idx = make_index(bucket, key, cache->capacity);

    for (int i = 0; i < cache->capacity; i++) {
        int probe = (idx + i) % cache->capacity;
        cache_entry_t *e = &cache->entries[probe];
        if (e->valid && strcmp(e->bucket, bucket) == 0 &&
            strcmp(e->key, key) == 0) {
            e->manifest = *manifest;
            return;
        }
    }

    if (cache->count >= cache->capacity) {
        int oldest_idx = 0;
        for (int i = 0; i < cache->capacity; i++) {
            if (!cache->entries[i].valid) {
                oldest_idx = i;
                break;
            }
        }
        cache->entries[oldest_idx].valid = 0;
        cache->count--;
    }

    for (int i = 0; i < cache->capacity; i++) {
        int probe = (idx + i) % cache->capacity;
        cache_entry_t *e = &cache->entries[probe];
        if (!e->valid) {
            strncpy(e->bucket, bucket, sizeof(e->bucket) - 1);
            strncpy(e->key, key, sizeof(e->key) - 1);
            e->manifest = *manifest;
            e->valid = 1;
            cache->count++;
            return;
        }
    }
}

void manifest_cache_invalidate(manifest_cache_t *cache,
                                const char *bucket, const char *key) {
    if (!cache || !bucket || !key) return;

    int idx = make_index(bucket, key, cache->capacity);

    for (int i = 0; i < cache->capacity; i++) {
        int probe = (idx + i) % cache->capacity;
        cache_entry_t *e = &cache->entries[probe];
        if (!e->valid) return;
        if (strcmp(e->bucket, bucket) == 0 &&
            strcmp(e->key, key) == 0) {
            e->valid = 0;
            cache->count--;
            return;
        }
    }
}

int manifest_cache_size(manifest_cache_t *cache) {
    return cache ? cache->count : 0;
}
