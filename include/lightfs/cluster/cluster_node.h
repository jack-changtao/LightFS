#ifndef LIGHTFS_CLUSTER_NODE_H
#define LIGHTFS_CLUSTER_NODE_H

#include "lightfs/cluster/cluster_types.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

#define NODE_LEASE_TTL 10
#define NODE_HEARTBEAT_INTERVAL_MILLISECONDS 3000

typedef struct cluster_node cluster_node_t;

typedef struct cluster_node_config {
  uint32_t node_id;
  uint32_t datacenter_id;
  const char *host;
  uint16_t gateway_port;
  uint16_t meta_port;
  uint16_t access_port;
  uint64_t disk_count;
  uint64_t total_disk_bytes;
} cluster_node_config_t;

cluster_node_t *cluster_node_join(etcd_client_t *client,
                 const cluster_node_config_t *config);
int cluster_node_heartbeat(cluster_node_t *node);
int cluster_node_leave(cluster_node_t *node);
const cluster_node_info_t *cluster_node_get_info(cluster_node_t *node);
void cluster_node_destroy(cluster_node_t *node);

#endif /* LIGHTFS_CLUSTER_NODE_H */
