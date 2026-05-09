#ifndef LIGHTFS_GATEWAY_REPLICATION_H
#define LIGHTFS_GATEWAY_REPLICATION_H

#include "lightfs/gateway/gateway_types.h"
#include "lightfs/meta/meta_types.h"
#include <stdint.h>

typedef struct replication_engine replication_engine_t;

typedef struct {
    uint32_t peer_dc_id;
    char peer_gateway_host[CLUSTER_MAX_HOST_LEN + 1];
    uint16_t peer_gateway_port;
} replication_peer_t;

replication_engine_t *replication_engine_create(void);
void replication_engine_destroy(replication_engine_t *engine);

int replication_add_peer(replication_engine_t *engine,
                          const replication_peer_t *peer);

int replication_enqueue(replication_engine_t *engine,
                         const object_manifest_t *manifest,
                         const uint8_t *data, uint64_t size);

int replication_resolve_conflict(uint64_t incoming_write_seq,
                                  uint32_t incoming_dc_id,
                                  uint64_t existing_write_seq,
                                  uint32_t existing_dc_id);

int replication_pending_count(replication_engine_t *engine);

#endif /* LIGHTFS_GATEWAY_REPLICATION_H */
