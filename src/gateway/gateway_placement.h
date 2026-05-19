#ifndef LIGHTFS_GATEWAY_PLACEMENT_INTERNAL_H
#define LIGHTFS_GATEWAY_PLACEMENT_INTERNAL_H

#include "lightfs/gateway/gateway_placement.h"

#define PLACEMENT_MAX_NODES 256

struct placement_engine {
  service_discovery_t *discovery;
  placement_target_t nodes[PLACEMENT_MAX_NODES];
  int node_count;
};

#endif /* LIGHTFS_GATEWAY_PLACEMENT_INTERNAL_H */
