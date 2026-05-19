#include "lightfs/meta/meta_server.h"
#include "lightfs/meta/meta_shard.h"
#include "meta_bucket_registry.h"
#include "lightfs/bs_cow_btree.h"
#include <stdlib.h>
#include <string.h>

struct meta_server {
  meta_server_config_t config;
  uint64_t write_sequence_counter;
  meta_shard_t *shard;
  bucket_registry_t *registry;
};

meta_server_t *meta_server_create(const meta_server_config_t *config) {
  if (!config) return NULL;

  meta_server_t *server = calloc(1, sizeof(meta_server_t));
  if (!server) return NULL;

  server->config = *config;
  server->write_sequence_counter = 0;

  server->shard = meta_shard_create(config->server_id, 0, "");
  if (!server->shard) {
    free(server);
    return NULL;
  }

  server->registry = bucket_registry_create();
  if (!server->registry) {
    meta_shard_destroy(server->shard);
    free(server);
    return NULL;
  }

  return server;
}

void meta_server_destroy(meta_server_t *server) {
  if (!server) return;
  if (server->shard) meta_shard_destroy(server->shard);
  if (server->registry) bucket_registry_destroy(server->registry);
  free(server);
}

int meta_server_push_manifest_batch(meta_server_t *server,
                  const manifest_batch_t *batch) {
  if (!server || !batch || !batch->manifests || batch->count <= 0) return -1;

  for (int i = 0; i < batch->count; i++) {
    object_manifest_t *manifest = &batch->manifests[i];

    manifest->write_sequence = ++server->write_sequence_counter;
    manifest->datacenter_id = server->config.datacenter_id;

    int result = meta_shard_insert(server->shard, manifest);
    if (result != 0) return -1;
  }

  return 0;
}

int meta_server_get_manifest(meta_server_t *server,
               const char *bucket, const char *key,
               object_manifest_t *manifest_out) {
  if (!server || !bucket || !key || !manifest_out) return -1;

  return meta_shard_lookup(server->shard, bucket, key, manifest_out);
}

int meta_server_list_objects(meta_server_t *server,
               const char *bucket,
               const char *prefix,
               const char *marker,
               int max_keys,
               char **keys_out,
               int *count_out) {
  if (!server || !bucket || !keys_out || !count_out) return -1;

  return meta_shard_list(server->shard, bucket, prefix, marker,
             max_keys, keys_out, count_out);
}

int meta_server_checkpoint(meta_server_t *server, uint32_t shard_id) {
  if (!server) return -1;
  (void)shard_id;
  return 0;
}

uint32_t meta_server_get_id(meta_server_t *server) {
  return server ? server->config.server_id : 0;
}
