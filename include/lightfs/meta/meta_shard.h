#ifndef LIGHTFS_META_SHARD_H
#define LIGHTFS_META_SHARD_H

#include "lightfs/meta/meta_types.h"
#include "lightfs/bs_cow_btree.h"

typedef struct meta_shard meta_shard_t;

meta_shard_t *meta_shard_create(uint32_t shard_id,
                uint32_t parent_shard_id,
                const char *bucket_name);

void meta_shard_destroy(meta_shard_t *shard);

int meta_shard_insert(meta_shard_t *shard, const object_manifest_t *manifest);

int meta_shard_lookup(meta_shard_t *shard,
           const char *bucket, const char *key,
           object_manifest_t *out);

int meta_shard_delete(meta_shard_t *shard,
           const char *bucket, const char *key);

int meta_shard_list(meta_shard_t *shard,
          const char *bucket,
          const char *prefix,
          const char *marker,
          int max_keys,
          char **keys_out,
          int *count_out);

int meta_shard_count(meta_shard_t *shard);

meta_shard_t *meta_shard_split(meta_shard_t *shard, uint32_t new_shard_id);

int meta_shard_has_loading_child(meta_shard_t *shard);

uint32_t meta_shard_get_id(meta_shard_t *shard);
int meta_shard_get_count(meta_shard_t *shard);
object_manifest_t *meta_shard_get_entries(meta_shard_t *shard);
void meta_shard_child_activated(meta_shard_t *shard);

#endif /* LIGHTFS_META_SHARD_H */
