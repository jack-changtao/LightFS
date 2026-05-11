#include "gateway_replication.h"
#include <stdlib.h>
#include <string.h>

replication_engine_t *replication_engine_create(void) {
    return calloc(1, sizeof(replication_engine_t));
}

void replication_engine_destroy(replication_engine_t *engine) {
    free(engine);
}

int replication_add_peer(replication_engine_t *engine,
                          const replication_peer_t *peer) {
    if (!engine || !peer || engine->peer_count >= REPLICATION_MAX_PEERS)
        return -1;
    engine->peers[engine->peer_count++] = *peer;
    return 0;
}

int replication_enqueue(replication_engine_t *engine,
                         const object_manifest_t *manifest,
                         const uint8_t *data, uint64_t size) {
    if (!engine || !manifest) return -1;
    if (engine->peer_count == 0) return -1;
    if (engine->queue_count >= REPLICATION_QUEUE_MAX) return -1;

    replication_entry_t *entry = &engine->queue[engine->queue_count++];
    entry->manifest = *manifest;
    entry->is_pending = 1;
    (void)data;
    (void)size;
    return 0;
}

int replication_resolve_conflict(uint64_t incoming_write_sequence,
                                  uint32_t incoming_datacenter_id,
                                  uint64_t existing_write_sequence,
                                  uint32_t existing_datacenter_id) {
    if (incoming_write_sequence > existing_write_sequence) return 1;
    if (incoming_write_sequence < existing_write_sequence) return 0;
    if (incoming_datacenter_id > existing_datacenter_id) return 1;
    return 0;
}

int replication_pending_count(replication_engine_t *engine) {
    return engine ? engine->queue_count : 0;
}
