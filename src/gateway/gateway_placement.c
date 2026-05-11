#include "gateway_placement.h"
#include <stdlib.h>
#include <string.h>

placement_engine_t *placement_engine_create(service_discovery_t *discovery) {
    if (!discovery) return NULL;

    placement_engine_t *engine = calloc(1, sizeof(placement_engine_t));
    if (!engine) return NULL;

    engine->discovery = discovery;
    return engine;
}

void placement_engine_destroy(placement_engine_t *engine) {
    free(engine);
}

int placement_register_node(placement_engine_t *engine,
                             uint32_t node_id, uint32_t datacenter_id,
                             uint32_t rack_id, uint32_t host_id,
                             uint64_t free_bytes) {
    if (!engine || engine->node_count >= PLACEMENT_MAX_NODES) return -1;

    placement_target_t *t = &engine->nodes[engine->node_count++];
    t->node_id = node_id;
    t->datacenter_id = datacenter_id;
    t->rack_id = rack_id;
    t->host_id = host_id;
    t->free_bytes = free_bytes;
    return 0;
}

int placement_select_targets(placement_engine_t *engine,
                              int data_fragment_count, int parity_fragment_count,
                              placement_target_t *targets_out,
                              int max_targets) {
    if (!engine || !targets_out) return -1;

    int needed = data_fragment_count + parity_fragment_count;
    if (needed > max_targets || needed > engine->node_count) return -1;

    for (int i = 0; i < needed; i++) {
        int index = i % engine->node_count;
        targets_out[i] = engine->nodes[index];
    }

    return needed;
}

int placement_get_domains(placement_engine_t *engine, uint32_t node_id,
                           uint32_t *datacenter_id, uint32_t *rack_id, uint32_t *host_id) {
    if (!engine) return -1;

    for (int i = 0; i < engine->node_count; i++) {
        if (engine->nodes[i].node_id == node_id) {
            if (datacenter_id) *datacenter_id = engine->nodes[i].datacenter_id;
            if (rack_id) *rack_id = engine->nodes[i].rack_id;
            if (host_id) *host_id = engine->nodes[i].host_id;
            return 0;
        }
    }
    return -1;
}
