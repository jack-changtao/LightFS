#include "gateway_internal.h"
#include <stdlib.h>
#include <string.h>

gateway_t *gateway_create(const gateway_config_t *config,
             service_discovery_t *discovery,
             etcd_client_t *etcd) {
  if (!config || !discovery || !etcd) return NULL;

  gateway_t *gateway = calloc(1, sizeof(gateway_t));
  if (!gateway) return NULL;

  gateway->config = *config;
  gateway->discovery = discovery;
  gateway->etcd = etcd;

  gateway->placement = placement_engine_create(discovery);
  if (!gateway->placement) {
    free(gateway);
    return NULL;
  }

  gateway->router = fragment_router_create(discovery);
  if (!gateway->router) {
    placement_engine_destroy(gateway->placement);
    free(gateway);
    return NULL;
  }

  gateway->cache = manifest_cache_create(config->manifest_cache_size > 0 ?
                   config->manifest_cache_size :
                   GATEWAY_MANIFEST_CACHE_SIZE);
  if (!gateway->cache) {
    fragment_router_destroy(gateway->router);
    placement_engine_destroy(gateway->placement);
    free(gateway);
    return NULL;
  }

  gateway->erasure_coding_engines[ERASURE_CODING_REPLICATION_2X] = erasure_coding_engine_create(1, 1);
  gateway->erasure_coding_engines[ERASURE_CODING_REPLICATION_3X] = erasure_coding_engine_create(1, 2);
  gateway->erasure_coding_engines[ERASURE_CODING_6_PLUS_3] = erasure_coding_engine_create(6, 3);
  gateway->erasure_coding_engines[ERASURE_CODING_10_PLUS_4] = erasure_coding_engine_create(10, 4);

  gateway->meta_batch.capacity = 64;
  gateway->meta_batch.manifests = calloc(gateway->meta_batch.capacity, sizeof(object_manifest_t));

  return gateway;
}

void gateway_destroy(gateway_t *gateway) {
  if (!gateway) return;
  for (int i = 0; i < 4; i++) {
    if (gateway->erasure_coding_engines[i]) erasure_coding_engine_destroy(gateway->erasure_coding_engines[i]);
  }
  free(gateway->meta_batch.manifests);
  manifest_cache_destroy(gateway->cache);
  fragment_router_destroy(gateway->router);
  placement_engine_destroy(gateway->placement);
  free(gateway);
}

static erasure_coding_policy_t select_erasure_coding_policy(gateway_t *gateway, uint64_t object_size) {
  if (object_size < GATEWAY_SMALL_THRESHOLD) {
    return gateway->config.default_replication_factor == 3 ?
       ERASURE_CODING_REPLICATION_3X : ERASURE_CODING_REPLICATION_2X;
  }
  if (object_size < GATEWAY_MEDIUM_THRESHOLD) return ERASURE_CODING_6_PLUS_3;
  return ERASURE_CODING_10_PLUS_4;
}

int gateway_put_object(gateway_t *gateway, const gateway_put_request_t *request) {
  if (!gateway || !request || !request->data) return -1;

  erasure_coding_policy_t policy = request->has_erasure_coding_override ?
            request->erasure_coding_policy_override :
            select_erasure_coding_policy(gateway, request->size);

  erasure_coding_engine_t *erasure_coding_engine = gateway->erasure_coding_engines[policy];
  if (!erasure_coding_engine) return -1;

  int data_fragment_count = erasure_coding_get_data_fragment_count(erasure_coding_engine);
  int parity_fragment_count = erasure_coding_get_parity_fragment_count(erasure_coding_engine);
  int total = data_fragment_count + parity_fragment_count;

  placement_target_t targets[GATEWAY_MAX_FRAGMENTS];
  int count = placement_select_targets(gateway->placement, data_fragment_count, parity_fragment_count, targets, total);
  if (count != total) return -1;

  uint8_t *fragment_data[GATEWAY_MAX_FRAGMENTS];
  uint64_t fragment_sizes[GATEWAY_MAX_FRAGMENTS];
  int result = erasure_coding_encode(erasure_coding_engine, request->data, request->size, fragment_data, fragment_sizes);
  if (result != 0) return -1;

  fragment_t fragments[GATEWAY_MAX_FRAGMENTS];
  for (int i = 0; i < total; i++) {
    fragments[i].fragment_index = (uint32_t)i;
    fragments[i].node_id = targets[i].node_id;
    fragments[i].disk_id = targets[i].disk_id;
    fragments[i].data = fragment_data[i];
    fragments[i].size = (uint32_t)fragment_sizes[i];
  }

  result = fragment_router_send(gateway->router, fragments, total, 3);
  for (int i = 0; i < total; i++) free(fragment_data[i]);
  if (result != 0) return -1;

  object_manifest_t manifest;
  memset(&manifest, 0, sizeof(manifest));
  strncpy(manifest.bucket, request->bucket, sizeof(manifest.bucket) - 1);
  strncpy(manifest.key, request->key, sizeof(manifest.key) - 1);
  manifest.size = request->size;
  manifest.fragment_count = (uint32_t)total;
  manifest.write_sequence = 0;
  manifest.datacenter_id = gateway->config.datacenter_id;

  manifest_cache_insert(gateway->cache, request->bucket, request->key, &manifest);

  if (gateway->meta_batch.count < gateway->meta_batch.capacity) {
    gateway->meta_batch.manifests[gateway->meta_batch.count++] = manifest;
  }

  return 0;
}

int gateway_get_object(gateway_t *gateway,
            const char *bucket, const char *key,
            gateway_get_response_t *response) {
  if (!gateway || !bucket || !key || !response) return -1;

  object_manifest_t manifest;
  int result = manifest_cache_lookup(gateway->cache, bucket, key, &manifest);
  if (result != 0) return -1;

  response->data = calloc(1, manifest.size > 0 ? manifest.size : 1);
  if (!response->data) return -1;
  response->size = manifest.size;
  response->error_code = 0;

  return 0;
}

int gateway_delete_object(gateway_t *gateway,
             const char *bucket, const char *key) {
  if (!gateway || !bucket || !key) return -1;

  manifest_cache_invalidate(gateway->cache, bucket, key);

  return 0;
}

void gateway_set_erasure_coding_policy(gateway_t *gateway, erasure_coding_policy_t policy) {
  if (gateway) gateway->config.default_erasure_coding_policy = policy;
}

struct placement_engine *gateway_get_placement(gateway_t *gateway) {
  return gateway ? gateway->placement : NULL;
}
