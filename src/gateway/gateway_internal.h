#ifndef LIGHTFS_GATEWAY_INTERNAL_H
#define LIGHTFS_GATEWAY_INTERNAL_H

#include "lightfs/gateway/gateway.h"
#include "gateway_ec.h"
#include "gateway_placement.h"
#include "gateway_fragment_router.h"
#include "gateway_manifest_cache.h"
#include "gateway_replication.h"
#include "lightfs/meta/meta_server.h"

struct gateway {
    gateway_config_t config;
    service_discovery_t *discovery;
    etcd_client_t *etcd;
    placement_engine_t *placement;
    fragment_router_t *router;
    manifest_cache_t *cache;
    erasure_coding_engine_t *erasure_coding_engines[4];
    meta_server_t *meta;
    manifest_batch_t meta_batch;
};

#endif /* LIGHTFS_GATEWAY_INTERNAL_H */
