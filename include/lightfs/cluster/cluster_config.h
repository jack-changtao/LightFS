#ifndef LIGHTFS_CLUSTER_CONFIG_H
#define LIGHTFS_CLUSTER_CONFIG_H

#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

#define CONFIG_EC_POLICIES     ETCD_PREFIX_CONFIG "/ec_policies"
#define CONFIG_REPLICATION     ETCD_PREFIX_CONFIG "/replication"
#define CONFIG_STORAGE_TIERS   ETCD_PREFIX_CONFIG "/storage_tiers"
#define CONFIG_LIFECYCLE       ETCD_PREFIX_CONFIG "/lifecycle_rules"

typedef struct {
  int default_data_k;
  int default_parity_m;
  uint64_t small_threshold;
  uint64_t medium_threshold;
} ec_policy_config_t;

typedef struct {
  char bucket_name[256];
  int replication_mode;
  ec_policy_config_t ec_policy;
  int lifecycle_enabled;
} bucket_config_t;

typedef struct cluster_config_manager cluster_config_manager_t;

cluster_config_manager_t *cluster_config_create(etcd_client_t *client);
void cluster_config_destroy(cluster_config_manager_t *manager);
int cluster_config_get_ec_policy(cluster_config_manager_t *manager,
                 ec_policy_config_t *out);
int cluster_config_get_bucket(cluster_config_manager_t *manager,
               const char *bucket,
               bucket_config_t *out);

typedef void (*cluster_config_callback)(const char *key, const char *new_value,
                 void *context);
int cluster_config_watch(cluster_config_manager_t *manager,
             cluster_config_callback callback, void *context);

#endif /* LIGHTFS_CLUSTER_CONFIG_H */
