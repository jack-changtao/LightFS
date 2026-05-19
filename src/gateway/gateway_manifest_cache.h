#ifndef LIGHTFS_GATEWAY_MANIFEST_CACHE_H
#define LIGHTFS_GATEWAY_MANIFEST_CACHE_H

#include "lightfs/meta/meta_types.h"
#include <stdint.h>

typedef struct manifest_cache manifest_cache_t;

manifest_cache_t *manifest_cache_create(int max_entries);
void manifest_cache_destroy(manifest_cache_t *cache);

int manifest_cache_lookup(manifest_cache_t *cache,
             const char *bucket, const char *key,
             object_manifest_t *out);

void manifest_cache_insert(manifest_cache_t *cache,
              const char *bucket, const char *key,
              const object_manifest_t *manifest);

void manifest_cache_invalidate(manifest_cache_t *cache,
                const char *bucket, const char *key);

int manifest_cache_size(manifest_cache_t *cache);

#endif /* LIGHTFS_GATEWAY_MANIFEST_CACHE_H */
