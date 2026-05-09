#ifndef LIGHTFS_GATEWAY_REPLICATION_INTERNAL_H
#define LIGHTFS_GATEWAY_REPLICATION_INTERNAL_H

#include "lightfs/gateway/gateway_replication.h"

#define REPLICATION_MAX_PEERS 8
#define REPLICATION_QUEUE_MAX 1024

typedef struct {
    object_manifest_t manifest;
    int pending;
} replication_entry_t;

struct replication_engine {
    replication_peer_t peers[REPLICATION_MAX_PEERS];
    int peer_count;
    replication_entry_t queue[REPLICATION_QUEUE_MAX];
    int queue_count;
};

#endif /* LIGHTFS_GATEWAY_REPLICATION_INTERNAL_H */
