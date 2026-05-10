#include "gateway_internal.h"
#include <stdlib.h>
#include <string.h>

gateway_t *gateway_create(const gateway_config_t *cfg,
                           service_discovery_t *sd,
                           etcd_client_t *etcd) {
    if (!cfg || !sd || !etcd) return NULL;

    gateway_t *gw = calloc(1, sizeof(gateway_t));
    if (!gw) return NULL;

    gw->config = *cfg;
    gw->sd = sd;
    gw->etcd = etcd;

    gw->placement = placement_engine_create(sd);
    if (!gw->placement) {
        free(gw);
        return NULL;
    }

    gw->router = fragment_router_create(sd);
    if (!gw->router) {
        placement_engine_destroy(gw->placement);
        free(gw);
        return NULL;
    }

    gw->cache = manifest_cache_create(cfg->manifest_cache_size > 0 ?
                                       cfg->manifest_cache_size :
                                       GATEWAY_MANIFEST_CACHE_SIZE);
    if (!gw->cache) {
        fragment_router_destroy(gw->router);
        placement_engine_destroy(gw->placement);
        free(gw);
        return NULL;
    }

    gw->ec_engines[EC_REPLICATION_2X] = ec_engine_create(1, 1);
    gw->ec_engines[EC_REPLICATION_3X] = ec_engine_create(1, 2);
    gw->ec_engines[EC_6_PLUS_3] = ec_engine_create(6, 3);
    gw->ec_engines[EC_10_PLUS_4] = ec_engine_create(10, 4);

    gw->meta_batch.capacity = 64;
    gw->meta_batch.manifests = calloc(gw->meta_batch.capacity, sizeof(object_manifest_t));

    return gw;
}

void gateway_destroy(gateway_t *gw) {
    if (!gw) return;
    for (int i = 0; i < 4; i++) {
        if (gw->ec_engines[i]) ec_engine_destroy(gw->ec_engines[i]);
    }
    free(gw->meta_batch.manifests);
    manifest_cache_destroy(gw->cache);
    fragment_router_destroy(gw->router);
    placement_engine_destroy(gw->placement);
    free(gw);
}

static ec_policy_t select_ec_policy(gateway_t *gw, uint64_t object_size) {
    if (object_size < GATEWAY_SMALL_THRESHOLD) {
        return gw->config.default_replication_factor == 3 ?
               EC_REPLICATION_3X : EC_REPLICATION_2X;
    }
    if (object_size < GATEWAY_MEDIUM_THRESHOLD) return EC_6_PLUS_3;
    return EC_10_PLUS_4;
}

int gateway_put_object(gateway_t *gw, const gateway_put_request_t *req) {
    if (!gw || !req || !req->data) return -1;

    ec_policy_t policy = req->ec_override ?
                         req->ec_policy_override :
                         select_ec_policy(gw, req->size);

    ec_engine_t *ec = gw->ec_engines[policy];
    if (!ec) return -1;

    int k = ec_get_k(ec);
    int m = ec_get_m(ec);
    int total = k + m;

    placement_target_t targets[GATEWAY_MAX_FRAGMENTS];
    int count = placement_select_targets(gw->placement, k, m, targets, total);
    if (count != total) return -1;

    uint8_t *frag_data[GATEWAY_MAX_FRAGMENTS];
    uint64_t frag_sizes[GATEWAY_MAX_FRAGMENTS];
    int rc = ec_encode(ec, req->data, req->size, frag_data, frag_sizes);
    if (rc != 0) return -1;

    fragment_t fragments[GATEWAY_MAX_FRAGMENTS];
    for (int i = 0; i < total; i++) {
        fragments[i].fragment_index = (uint32_t)i;
        fragments[i].node_id = targets[i].node_id;
        fragments[i].disk_id = targets[i].disk_id;
        fragments[i].data = frag_data[i];
        fragments[i].size = (uint32_t)frag_sizes[i];
    }

    rc = fragment_router_send(gw->router, fragments, total, 3);
    for (int i = 0; i < total; i++) free(frag_data[i]);
    if (rc != 0) return -1;

    object_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    strncpy(manifest.bucket, req->bucket, sizeof(manifest.bucket) - 1);
    strncpy(manifest.key, req->key, sizeof(manifest.key) - 1);
    manifest.size = req->size;
    manifest.fragment_count = (uint32_t)total;
    manifest.write_seq = 0;
    manifest.dc_id = gw->config.dc_id;

    manifest_cache_insert(gw->cache, req->bucket, req->key, &manifest);

    if (gw->meta_batch.count < gw->meta_batch.capacity) {
        gw->meta_batch.manifests[gw->meta_batch.count++] = manifest;
    }

    return 0;
}

int gateway_get_object(gateway_t *gw,
                        const char *bucket, const char *key,
                        gateway_get_response_t *resp) {
    if (!gw || !bucket || !key || !resp) return -1;

    object_manifest_t manifest;
    int rc = manifest_cache_lookup(gw->cache, bucket, key, &manifest);
    if (rc != 0) return -1;

    resp->data = calloc(1, manifest.size > 0 ? manifest.size : 1);
    if (!resp->data) return -1;
    resp->size = manifest.size;
    resp->rc = 0;

    return 0;
}

int gateway_delete_object(gateway_t *gw,
                           const char *bucket, const char *key) {
    if (!gw || !bucket || !key) return -1;

    manifest_cache_invalidate(gw->cache, bucket, key);

    return 0;
}

void gateway_set_ec_policy(gateway_t *gw, ec_policy_t policy) {
    if (gw) gw->config.default_ec_policy = policy;
}

struct placement_engine *gateway_get_placement(gateway_t *gw) {
    return gw ? gw->placement : NULL;
}
