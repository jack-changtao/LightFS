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
                                   const cluster_node_config_t *cfg) {
    if (!client || !cfg) return NULL;

    cluster_node_t *node = calloc(1, sizeof(cluster_node_t));
    if (!node) return NULL;

    node->client = client;
    node->info.node_id = cfg->node_id;
    node->info.dc_id = cfg->dc_id;
    strncpy(node->info.host, cfg->host, sizeof(node->info.host) - 1);
    node->info.gateway_port = cfg->gateway_port;
    node->info.meta_port = cfg->meta_port;
    node->info.access_port = cfg->access_port;
    node->info.disk_count = cfg->disk_count;
    node->info.total_disk_bytes = cfg->total_disk_bytes;
    node->info.status = NODE_ACTIVE;

    if (etcd_lease_grant(client, NODE_LEASE_TTL, &node->lease) != 0) {
        free(node);
        return NULL;
    }

    snprintf(node->node_key, sizeof(node->node_key),
             ETCD_PREFIX_TOPOLOGY "/node/%u", cfg->node_id);

    char node_json[2048];
    snprintf(node_json, sizeof(node_json),
             "{\"node_id\":%u,\"dc_id\":%u,\"host\":\"%s\","
             "\"gateway_port\":%u,\"meta_port\":%u,\"access_port\":%u,"
             "\"disk_count\":%lu,\"total_disk_bytes\":%lu,\"status\":\"active\"}",
             cfg->node_id, cfg->dc_id, cfg->host,
             cfg->gateway_port, cfg->meta_port, cfg->access_port,
             (unsigned long)cfg->disk_count,
             (unsigned long)cfg->total_disk_bytes);

    if (etcd_kv_put(client, node->node_key, node_json, node->lease.id) != 0) {
        etcd_lease_revoke(client, node->lease.id);
        free(node);
        return NULL;
    }

    char svc_key[1024];
    char svc_val[256];

    snprintf(svc_key, sizeof(svc_key),
             ETCD_PREFIX_DISCOVERY "/gateways/node_%u", cfg->node_id);
    snprintf(svc_val, sizeof(svc_val), "%s:%u", cfg->host, cfg->gateway_port);
    etcd_kv_put(client, svc_key, svc_val, node->lease.id);

    snprintf(svc_key, sizeof(svc_key),
             ETCD_PREFIX_DISCOVERY "/meta/node_%u", cfg->node_id);
    snprintf(svc_val, sizeof(svc_val), "%s:%u", cfg->host, cfg->meta_port);
    etcd_kv_put(client, svc_key, svc_val, node->lease.id);

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

    etcd_kv_put(node->client, node->node_key, node_json, node->lease.id);

    return etcd_lease_revoke(node->client, node->lease.id);
}

const cluster_node_info_t *cluster_node_get_info(cluster_node_t *node) {
    return node ? &node->info : NULL;
}

void cluster_node_destroy(cluster_node_t *node) {
    free(node);
}
