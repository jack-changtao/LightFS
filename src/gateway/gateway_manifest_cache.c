#include "gateway_manifest_cache.h"
#include <stdlib.h>
#include <string.h>

typedef struct cache_entry {
  char bucket[META_MAX_BUCKET_LENGTH + 1];
  char key[META_MAX_KEY_LENGTH + 1];
  object_manifest_t manifest;
  int is_valid;
} cache_entry_t;

struct manifest_cache {
  cache_entry_t *entries;
  int capacity;
  int count;
};

manifest_cache_t *manifest_cache_create(int max_entries) {
  manifest_cache_t *cache = calloc(1, sizeof(manifest_cache_t));
  if (!cache) return NULL;

  cache->capacity = max_entries;
  cache->entries = calloc(max_entries, sizeof(cache_entry_t));
  if (!cache->entries) {
    free(cache);
    return NULL;
  }
  return cache;
}

void manifest_cache_destroy(manifest_cache_t *cache) {
  if (!cache) return;
  free(cache->entries);
  free(cache);
}

static int make_index(const char *bucket, const char *key, int capacity) {
  int hash = 0;
  while (*bucket) hash = (hash * 31 + *bucket++) & 0x7FFFFFFF;
  while (*key) hash = (hash * 31 + *key++) & 0x7FFFFFFF;
  return hash % capacity;
}

int manifest_cache_lookup(manifest_cache_t *cache,
             const char *bucket, const char *key,
             object_manifest_t *out) {
  if (!cache || !bucket || !key || !out) return -1;

  int index = make_index(bucket, key, cache->capacity);

  for (int i = 0; i < cache->capacity; i++) {
    int probe = (index + i) % cache->capacity;
    cache_entry_t *entry = &cache->entries[probe];
    if (!entry->is_valid) return -1;
    if (strcmp(entry->bucket, bucket) == 0 &&
      strcmp(entry->key, key) == 0) {
      *out = entry->manifest;
      return 0;
    }
  }
  return -1;
}

void manifest_cache_insert(manifest_cache_t *cache,
              const char *bucket, const char *key,
              const object_manifest_t *manifest) {
  if (!cache || !bucket || !key || !manifest) return;

  int index = make_index(bucket, key, cache->capacity);

  for (int i = 0; i < cache->capacity; i++) {
    int probe = (index + i) % cache->capacity;
    cache_entry_t *entry = &cache->entries[probe];
    if (entry->is_valid && strcmp(entry->bucket, bucket) == 0 &&
      strcmp(entry->key, key) == 0) {
      entry->manifest = *manifest;
      return;
    }
  }

  if (cache->count >= cache->capacity) {
    int oldest_index = 0;
    for (int i = 0; i < cache->capacity; i++) {
      if (!cache->entries[i].is_valid) {
        oldest_index = i;
        break;
      }
    }
    cache->entries[oldest_index].is_valid = 0;
    cache->count--;
  }

  for (int i = 0; i < cache->capacity; i++) {
    int probe = (index + i) % cache->capacity;
    cache_entry_t *entry = &cache->entries[probe];
    if (!entry->is_valid) {
      strncpy(entry->bucket, bucket, sizeof(entry->bucket) - 1);
      strncpy(entry->key, key, sizeof(entry->key) - 1);
      entry->manifest = *manifest;
      entry->is_valid = 1;
      cache->count++;
      return;
    }
  }
}

void manifest_cache_invalidate(manifest_cache_t *cache,
                const char *bucket, const char *key) {
  if (!cache || !bucket || !key) return;

  int index = make_index(bucket, key, cache->capacity);

  for (int i = 0; i < cache->capacity; i++) {
    int probe = (index + i) % cache->capacity;
    cache_entry_t *entry = &cache->entries[probe];
    if (!entry->is_valid) return;
    if (strcmp(entry->bucket, bucket) == 0 &&
      strcmp(entry->key, key) == 0) {
      entry->is_valid = 0;
      cache->count--;
      return;
    }
  }
}

int manifest_cache_size(manifest_cache_t *cache) {
  return cache ? cache->count : 0;
}
