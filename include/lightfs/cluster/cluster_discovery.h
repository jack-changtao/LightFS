#ifndef LIGHTFS_CLUSTER_DISCOVERY_H
#define LIGHTFS_CLUSTER_DISCOVERY_H

#include "lightfs/cluster/cluster_types.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

typedef struct service_discovery service_discovery_t;

service_discovery_t *service_discovery_create(etcd_client_t *client);
void service_discovery_destroy(service_discovery_t *sd);

int service_discovery_get_gateways(service_discovery_t *sd,
                                    service_endpoint_t *endpoints,
                                    int max_endpoints);
int service_discovery_get_storage_engines(service_discovery_t *sd,
                                           service_endpoint_t *endpoints,
                                           int max_endpoints);
int service_discovery_get_meta_servers(service_discovery_t *sd,
                                        service_endpoint_t *endpoints,
                                        int max_endpoints);
int service_discovery_get_shards(service_discovery_t *sd,
                                  cluster_shard_info_t *shards,
                                  int max_shards);

typedef void (*topology_change_cb)(void *ctx);
int service_discovery_watch(service_discovery_t *sd,
                             topology_change_cb cb, void *ctx);

#endif /* LIGHTFS_CLUSTER_DISCOVERY_H */
