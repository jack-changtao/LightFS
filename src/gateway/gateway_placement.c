#include "gateway_placement.h"
#include <stdlib.h>
#include <string.h>

placement_engine_t *placement_engine_create(service_discovery_t *sd) {
    if (!sd) return NULL;

    placement_engine_t *pe = calloc(1, sizeof(placement_engine_t));
    if (!pe) return NULL;

    pe->sd = sd;
    return pe;
}

void placement_engine_destroy(placement_engine_t *engine) {
    free(engine);
}

int placement_register_node(placement_engine_t *engine,
                             uint32_t node_id, uint32_t dc_id,
                             uint32_t rack_id, uint32_t host_id,
                             uint64_t free_bytes) {
    if (!engine || engine->node_count >= PLACEMENT_MAX_NODES) return -1;

    placement_target_t *t = &engine->nodes[engine->node_count++];
    t->node_id = node_id;
    t->dc_id = dc_id;
    t->rack_id = rack_id;
    t->host_id = host_id;
    t->free_bytes = free_bytes;
    return 0;
}

int placement_select_targets(placement_engine_t *engine,
                              int k, int m,
                              placement_target_t *targets_out,
                              int max_targets) {
    if (!engine || !targets_out) return -1;

    int needed = k + m;
    if (needed > max_targets || needed > engine->node_count) return -1;

    for (int i = 0; i < needed; i++) {
        int idx = i % engine->node_count;
        targets_out[i] = engine->nodes[idx];
    }

    return needed;
}

int placement_get_domains(placement_engine_t *engine, uint32_t node_id,
                           uint32_t *dc_id, uint32_t *rack_id, uint32_t *host_id) {
    if (!engine) return -1;

    for (int i = 0; i < engine->node_count; i++) {
        if (engine->nodes[i].node_id == node_id) {
            if (dc_id) *dc_id = engine->nodes[i].dc_id;
            if (rack_id) *rack_id = engine->nodes[i].rack_id;
            if (host_id) *host_id = engine->nodes[i].host_id;
            return 0;
        }
    }
    return -1;
}
