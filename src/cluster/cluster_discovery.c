#include "lightfs/cluster/cluster_discovery.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ENDPOINTS 256
#define MAX_SHARDS 1024

struct service_discovery {
    etcd_client_t *client;
    service_endpoint_t gateways[MAX_ENDPOINTS];
    service_endpoint_t storage_engines[MAX_ENDPOINTS];
    service_endpoint_t meta_servers[MAX_ENDPOINTS];
    cluster_shard_info_t shards[MAX_SHARDS];
    int gateway_count;
    int storage_engine_count;
    int meta_count;
    int shard_count;
    topology_change_callback topology_callback;
    void *topology_context;
};

service_discovery_t *service_discovery_create(etcd_client_t *client) {
    if (!client) return NULL;

    service_discovery_t *discovery = calloc(1, sizeof(service_discovery_t));
    if (!discovery) return NULL;

    discovery->client = client;

    etcd_key_value_response_t *results = NULL;
    int count = 0;

    if (etcd_key_value_list(client, ETCD_PREFIX_DISCOVERY "/gateways/", &results, &count) == 0) {
        for (int i = 0; i < count && discovery->gateway_count < MAX_ENDPOINTS; i++) {
            if (results[i].value) {
                discovery->gateways[discovery->gateway_count].node_id = (uint32_t)(i + 1);
                strncpy(discovery->gateways[discovery->gateway_count].host, results[i].value,
                        sizeof(discovery->gateways[discovery->gateway_count].host) - 1);
                discovery->gateways[discovery->gateway_count].port = 8080;
                discovery->gateway_count++;
                free(results[i].key);
                free(results[i].value);
            }
        }
        free(results);
    }

    if (etcd_key_value_list(client, ETCD_PREFIX_DISCOVERY "/meta/", &results, &count) == 0) {
        for (int i = 0; i < count && discovery->meta_count < MAX_ENDPOINTS; i++) {
            if (results[i].value) {
                discovery->meta_servers[discovery->meta_count].node_id = (uint32_t)(i + 1);
                strncpy(discovery->meta_servers[discovery->meta_count].host, results[i].value,
                        sizeof(discovery->meta_servers[discovery->meta_count].host) - 1);
                discovery->meta_servers[discovery->meta_count].port = 9090;
                discovery->meta_count++;
                free(results[i].key);
                free(results[i].value);
            }
        }
        free(results);
    }

    return discovery;
}

void service_discovery_destroy(service_discovery_t *discovery) {
    free(discovery);
}

int service_discovery_get_gateways(service_discovery_t *discovery,
                                    service_endpoint_t *endpoints,
                                    int max_endpoints) {
    if (!discovery || !endpoints) return -1;

    int count = discovery->gateway_count < max_endpoints ?
                discovery->gateway_count : max_endpoints;
    memcpy(endpoints, discovery->gateways, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_storage_engines(service_discovery_t *discovery,
                                           service_endpoint_t *endpoints,
                                           int max_endpoints) {
    if (!discovery || !endpoints) return -1;

    int count = discovery->storage_engine_count < max_endpoints ?
                discovery->storage_engine_count : max_endpoints;
    memcpy(endpoints, discovery->storage_engines, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_meta_servers(service_discovery_t *discovery,
                                        service_endpoint_t *endpoints,
                                        int max_endpoints) {
    if (!discovery || !endpoints) return -1;

    int count = discovery->meta_count < max_endpoints ?
                discovery->meta_count : max_endpoints;
    memcpy(endpoints, discovery->meta_servers, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_shards(service_discovery_t *discovery,
                                  cluster_shard_info_t *shards,
                                  int max_shards) {
    if (!discovery || !shards) return -1;

    int count = discovery->shard_count < max_shards ?
                discovery->shard_count : max_shards;
    memcpy(shards, discovery->shards, count * sizeof(cluster_shard_info_t));
    return count;
}

int service_discovery_watch(service_discovery_t *discovery,
                             topology_change_callback callback, void *context) {
    if (!discovery || !callback) return -1;
    discovery->topology_callback = callback;
    discovery->topology_context = context;
    return 0;
}
