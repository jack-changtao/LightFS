#include "lightfs/cluster/cluster_config.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BUCKET_CONFIGS 1000

struct cluster_config_manager {
    etcd_client_t *client;
    ec_policy_config_t default_ec;
    bucket_config_t bucket_configs[MAX_BUCKET_CONFIGS];
    int bucket_count;
    cluster_config_cb watch_cb;
    void *watch_ctx;
};

cluster_config_manager_t *cluster_config_create(etcd_client_t *client) {
    if (!client) return NULL;

    cluster_config_manager_t *mgr = calloc(1, sizeof(cluster_config_manager_t));
    if (!mgr) return NULL;

    mgr->client = client;

    mgr->default_ec.default_data_k = 10;
    mgr->default_ec.default_parity_m = 4;
    mgr->default_ec.small_threshold = 4ULL * 1024 * 1024;
    mgr->default_ec.medium_threshold = 64ULL * 1024 * 1024;

    return mgr;
}

void cluster_config_destroy(cluster_config_manager_t *mgr) {
    free(mgr);
}

int cluster_config_get_ec_policy(cluster_config_manager_t *mgr,
                                  ec_policy_config_t *out) {
    if (!mgr || !out) return -1;
    *out = mgr->default_ec;
    return 0;
}

int cluster_config_get_bucket(cluster_config_manager_t *mgr,
                               const char *bucket,
                               bucket_config_t *out) {
    if (!mgr || !bucket || !out) return -1;

    for (int i = 0; i < mgr->bucket_count; i++) {
        if (strcmp(mgr->bucket_configs[i].bucket_name, bucket) == 0) {
            *out = mgr->bucket_configs[i];
            return 0;
        }
    }
    return -1;
}

int cluster_config_watch(cluster_config_manager_t *mgr,
                          cluster_config_cb cb, void *ctx) {
    if (!mgr || !cb) return -1;
    mgr->watch_cb = cb;
    mgr->watch_ctx = ctx;
    return 0;
}
