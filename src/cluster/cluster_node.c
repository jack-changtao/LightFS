#include "lightfs/cluster/cluster_node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct cluster_node {
    etcd_client_t *client;
    cluster_node_info_t info;
    etcd_lease_t lease;
    char node_key[1024];
};

cluster_node_t *cluster_node_join(etcd_client_t *client,
                                   const cluster_node_config_t *config) {
    if (!client || !config) return NULL;

    cluster_node_t *node = calloc(1, sizeof(cluster_node_t));
    if (!node) return NULL;

    node->client = client;
    node->info.node_id = config->node_id;
    node->info.datacenter_id = config->datacenter_id;
    strncpy(node->info.host, config->host, sizeof(node->info.host) - 1);
    node->info.gateway_port = config->gateway_port;
    node->info.meta_port = config->meta_port;
    node->info.access_port = config->access_port;
    node->info.disk_count = config->disk_count;
    node->info.total_disk_bytes = config->total_disk_bytes;
    node->info.status = NODE_ACTIVE;

    if (etcd_lease_grant(client, NODE_LEASE_TTL, &node->lease) != 0) {
        free(node);
        return NULL;
    }

    snprintf(node->node_key, sizeof(node->node_key),
             ETCD_PREFIX_TOPOLOGY "/node/%u", config->node_id);

    char node_json[2048];
    snprintf(node_json, sizeof(node_json),
             "{\"node_id\":%u,\"datacenter_id\":%u,\"host\":\"%s\","
             "\"gateway_port\":%u,\"meta_port\":%u,\"access_port\":%u,"
             "\"disk_count\":%lu,\"total_disk_bytes\":%lu,\"status\":\"active\"}",
             config->node_id, config->datacenter_id, config->host,
             config->gateway_port, config->meta_port, config->access_port,
             (unsigned long)config->disk_count,
             (unsigned long)config->total_disk_bytes);

    if (etcd_key_value_put(client, node->node_key, node_json, node->lease.id) != 0) {
        etcd_lease_revoke(client, node->lease.id);
        free(node);
        return NULL;
    }

    char service_key[1024];
    char service_value[256];

    snprintf(service_key, sizeof(service_key),
             ETCD_PREFIX_DISCOVERY "/gateways/node_%u", config->node_id);
    snprintf(service_value, sizeof(service_value), "%s:%u", config->host, config->gateway_port);
    etcd_key_value_put(client, service_key, service_value, node->lease.id);

    snprintf(service_key, sizeof(service_key),
             ETCD_PREFIX_DISCOVERY "/meta/node_%u", config->node_id);
    snprintf(service_value, sizeof(service_value), "%s:%u", config->host, config->meta_port);
    etcd_key_value_put(client, service_key, service_value, node->lease.id);

    return node;
}

int cluster_node_heartbeat(cluster_node_t *node) {
    if (!node) return -1;
    return etcd_lease_keepalive(node->client, node->lease.id);
}

int cluster_node_leave(cluster_node_t *node) {
    if (!node) return -1;

    node->info.status = NODE_DRAINING;

    char node_json[2048];
    snprintf(node_json, sizeof(node_json),
             "{\"node_id\":%u,\"status\":\"draining\"}",
             node->info.node_id);

    etcd_key_value_put(node->client, node->node_key, node_json, node->lease.id);

    return etcd_lease_revoke(node->client, node->lease.id);
}

const cluster_node_info_t *cluster_node_get_info(cluster_node_t *node) {
    return node ? &node->info : NULL;
}

void cluster_node_destroy(cluster_node_t *node) {
    free(node);
}
