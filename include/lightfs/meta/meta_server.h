#ifndef LIGHTFS_META_SERVER_H
#define LIGHTFS_META_SERVER_H

#include "lightfs/meta/meta_types.h"
#include <stdint.h>

typedef struct meta_server meta_server_t;

typedef struct meta_server_config {
    uint32_t server_id;
    uint32_t datacenter_id;
    uint64_t split_threshold;
    uint32_t checkpoint_interval_milliseconds;
} meta_server_config_t;

meta_server_t *meta_server_create(const meta_server_config_t *config);
void meta_server_destroy(meta_server_t *server);

int meta_server_push_manifest_batch(meta_server_t *server,
                                     const manifest_batch_t *batch);

int meta_server_get_manifest(meta_server_t *server,
                              const char *bucket, const char *key,
                              object_manifest_t *manifest_out);

int meta_server_list_objects(meta_server_t *server,
                              const char *bucket,
                              const char *prefix,
                              const char *marker,
                              int max_keys,
                              char **keys_out,
                              int *count_out);

int meta_server_checkpoint(meta_server_t *server, uint32_t shard_id);

uint32_t meta_server_get_id(meta_server_t *server);

#endif /* LIGHTFS_META_SERVER_H */
