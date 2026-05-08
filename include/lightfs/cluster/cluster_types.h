#ifndef LIGHTFS_CLUSTER_TYPES_H
#define LIGHTFS_CLUSTER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define CLUSTER_MAX_HOST_LEN 256
#define CLUSTER_MAX_PATH_LEN 1024
#define CLUSTER_ETCD_PREFIX "/lightfs"

typedef enum {
    NODE_ACTIVE = 0,
    NODE_DRAINING,
    NODE_DOWN,
} node_status_t;

typedef struct cluster_node_info {
    uint32_t node_id;
    uint32_t dc_id;
    char host[CLUSTER_MAX_HOST_LEN + 1];
    uint16_t gateway_port;
    uint16_t meta_port;
    uint16_t access_port;
    node_status_t status;
    uint64_t disk_count;
    uint64_t total_disk_bytes;
} cluster_node_info_t;

typedef struct cluster_shard_info {
    uint32_t shard_id;
    uint32_t owner_meta_server_id;
    node_status_t status;
    char key_min[CLUSTER_MAX_PATH_LEN + 1];
    char key_max[CLUSTER_MAX_PATH_LEN + 1];
    uint32_t parent_shard_id;
} cluster_shard_info_t;

typedef struct service_endpoint {
    uint32_t node_id;
    char host[CLUSTER_MAX_HOST_LEN + 1];
    uint16_t port;
    char service_name[64];
} service_endpoint_t;

#define ETCD_PREFIX_TOPOLOGY      "/lightfs/cluster/topology"
#define ETCD_PREFIX_DISCOVERY     "/lightfs/cluster/discovery"
#define ETCD_PREFIX_META_SHARDS  "/lightfs/meta/shards"
#define ETCD_PREFIX_CONFIG        "/lightfs/cluster/config"

#endif /* LIGHTFS_CLUSTER_TYPES_H */
