#ifndef LIGHTFS_GATEWAY_FRAGMENT_ROUTER_H
#define LIGHTFS_GATEWAY_FRAGMENT_ROUTER_H

#include "lightfs/gateway/gateway_types.h"
#include "lightfs/cluster/cluster_discovery.h"
#include <stdint.h>

typedef struct fragment_router fragment_router_t;

fragment_router_t *fragment_router_create(service_discovery_t *sd);
void fragment_router_destroy(fragment_router_t *router);

int fragment_router_send(fragment_router_t *router,
                          fragment_t *fragments, int fragment_count,
                          int max_retries);

int fragment_router_read(fragment_router_t *router,
                          blob_location_t *locations, int count,
                          uint8_t **data_out, uint32_t *sizes_out);

#endif /* LIGHTFS_GATEWAY_FRAGMENT_ROUTER_H */
