#ifndef LIGHTFS_META_BUCKET_REGISTRY_H
#define LIGHTFS_META_BUCKET_REGISTRY_H

#include "lightfs/meta/meta_types.h"

typedef struct bucket_registry bucket_registry_t;

bucket_registry_t *bucket_registry_create(void);
void bucket_registry_destroy(bucket_registry_t *registry);

int bucket_registry_add(bucket_registry_t *registry, const bucket_entry_t *entry);

int bucket_registry_lookup(bucket_registry_t *registry, const char *bucket,
              bucket_entry_t *entry_out);

#endif /* LIGHTFS_META_BUCKET_REGISTRY_H */
