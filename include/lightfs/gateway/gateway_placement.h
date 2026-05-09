#ifndef LIGHTFS_GATEWAY_PLACEMENT_H
#define LIGHTFS_GATEWAY_PLACEMENT_H

#include "lightfs/gateway/gateway_types.h"
#include "lightfs/cluster/cluster_discovery.h"
#include <stdint.h>

typedef struct placement_engine placement_engine_t;

placement_engine_t *placement_engine_create(service_discovery_t *sd);
void placement_engine_destroy(placement_engine_t *engine);

int placement_select_targets(placement_engine_t *engine,
                              int k, int m,
                              placement_target_t *targets_out,
                              int max_targets);

int placement_get_domains(placement_engine_t *engine, uint32_t node_id,
                           uint32_t *dc_id, uint32_t *rack_id, uint32_t *host_id);

int placement_register_node(placement_engine_t *engine,
                             uint32_t node_id, uint32_t dc_id,
                             uint32_t rack_id, uint32_t host_id,
                             uint64_t free_bytes);

#endif /* LIGHTFS_GATEWAY_PLACEMENT_H */
