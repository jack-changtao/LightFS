#ifndef LIGHTFS_GATEWAY_H
#define LIGHTFS_GATEWAY_H

#include "lightfs/gateway/gateway_types.h"
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

typedef struct gateway gateway_t;

gateway_t *gateway_create(const gateway_config_t *config,
             service_discovery_t *discovery,
             etcd_client_t *etcd);

void gateway_destroy(gateway_t *gateway);

int gateway_put_object(gateway_t *gateway, const gateway_put_request_t *request);

int gateway_get_object(gateway_t *gateway,
            const char *bucket, const char *key,
            gateway_get_response_t *response);

int gateway_delete_object(gateway_t *gateway,
             const char *bucket, const char *key);

void gateway_set_erasure_coding_policy(gateway_t *gateway, erasure_coding_policy_t policy);

struct placement_engine;
struct placement_engine *gateway_get_placement(gateway_t *gateway);

#endif /* LIGHTFS_GATEWAY_H */
