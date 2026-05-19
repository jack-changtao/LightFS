#include "lightfs/cluster/cluster_config.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BUCKET_CONFIGS 1000

struct cluster_config_manager {
  etcd_client_t *client;
  ec_policy_config_t default_ec;
  bucket_config_t bucket_configs[MAX_BUCKET_CONFIGS];
  int bucket_count;
  cluster_config_callback watch_callback;
  void *watch_context;
};

cluster_config_manager_t *cluster_config_create(etcd_client_t *client) {
  if (!client) return NULL;

  cluster_config_manager_t *manager = calloc(1, sizeof(cluster_config_manager_t));
  if (!manager) return NULL;

  manager->client = client;

  manager->default_ec.default_data_k = 10;
  manager->default_ec.default_parity_m = 4;
  manager->default_ec.small_threshold = 4ULL * 1024 * 1024;
  manager->default_ec.medium_threshold = 64ULL * 1024 * 1024;

  return manager;
}

void cluster_config_destroy(cluster_config_manager_t *manager) {
  free(manager);
}

int cluster_config_get_ec_policy(cluster_config_manager_t *manager,
                 ec_policy_config_t *out) {
  if (!manager || !out) return -1;
  *out = manager->default_ec;
  return 0;
}

int cluster_config_get_bucket(cluster_config_manager_t *manager,
               const char *bucket,
               bucket_config_t *out) {
  if (!manager || !bucket || !out) return -1;

  for (int i = 0; i < manager->bucket_count; i++) {
    if (strcmp(manager->bucket_configs[i].bucket_name, bucket) == 0) {
      *out = manager->bucket_configs[i];
      return 0;
    }
  }
  return -1;
}

int cluster_config_watch(cluster_config_manager_t *manager,
             cluster_config_callback callback, void *context) {
  if (!manager || !callback) return -1;
  manager->watch_callback = callback;
  manager->watch_context = context;
  return 0;
}
