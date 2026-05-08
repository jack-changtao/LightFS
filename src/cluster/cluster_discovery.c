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
    int se_count;
    int meta_count;
    int shard_count;
    topology_change_cb topo_cb;
    void *topo_ctx;
};

service_discovery_t *service_discovery_create(etcd_client_t *client) {
    if (!client) return NULL;

    service_discovery_t *sd = calloc(1, sizeof(service_discovery_t));
    if (!sd) return NULL;

    sd->client = client;

    etcd_kv_response_t *results = NULL;
    int count = 0;

    if (etcd_kv_list(client, ETCD_PREFIX_DISCOVERY "/gateways/", &results, &count) == 0) {
        for (int i = 0; i < count && sd->gateway_count < MAX_ENDPOINTS; i++) {
            if (results[i].value) {
                sd->gateways[sd->gateway_count].node_id = (uint32_t)(i + 1);
                strncpy(sd->gateways[sd->gateway_count].host, results[i].value,
                        sizeof(sd->gateways[sd->gateway_count].host) - 1);
                sd->gateways[sd->gateway_count].port = 8080;
                sd->gateway_count++;
                free(results[i].key);
                free(results[i].value);
            }
        }
        free(results);
    }

    if (etcd_kv_list(client, ETCD_PREFIX_DISCOVERY "/meta/", &results, &count) == 0) {
        for (int i = 0; i < count && sd->meta_count < MAX_ENDPOINTS; i++) {
            if (results[i].value) {
                sd->meta_servers[sd->meta_count].node_id = (uint32_t)(i + 1);
                strncpy(sd->meta_servers[sd->meta_count].host, results[i].value,
                        sizeof(sd->meta_servers[sd->meta_count].host) - 1);
                sd->meta_servers[sd->meta_count].port = 9090;
                sd->meta_count++;
                free(results[i].key);
                free(results[i].value);
            }
        }
        free(results);
    }

    return sd;
}

void service_discovery_destroy(service_discovery_t *sd) {
    free(sd);
}

int service_discovery_get_gateways(service_discovery_t *sd,
                                    service_endpoint_t *endpoints,
                                    int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->gateway_count < max_endpoints ?
                sd->gateway_count : max_endpoints;
    memcpy(endpoints, sd->gateways, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_storage_engines(service_discovery_t *sd,
                                           service_endpoint_t *endpoints,
                                           int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->se_count < max_endpoints ?
                sd->se_count : max_endpoints;
    memcpy(endpoints, sd->storage_engines, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_meta_servers(service_discovery_t *sd,
                                        service_endpoint_t *endpoints,
                                        int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->meta_count < max_endpoints ?
                sd->meta_count : max_endpoints;
    memcpy(endpoints, sd->meta_servers, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_shards(service_discovery_t *sd,
                                  cluster_shard_info_t *shards,
                                  int max_shards) {
    if (!sd || !shards) return -1;

    int count = sd->shard_count < max_shards ?
                sd->shard_count : max_shards;
    memcpy(shards, sd->shards, count * sizeof(cluster_shard_info_t));
    return count;
}

int service_discovery_watch(service_discovery_t *sd,
                             topology_change_cb cb, void *ctx) {
    if (!sd || !cb) return -1;
    sd->topo_cb = cb;
    sd->topo_ctx = ctx;
    return 0;
}
