#ifndef LIGHTFS_GATEWAY_H
#define LIGHTFS_GATEWAY_H

#include "lightfs/gateway/gateway_types.h"
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

typedef struct gateway gateway_t;

gateway_t *gateway_create(const gateway_config_t *cfg,
                           service_discovery_t *sd,
                           etcd_client_t *etcd);

void gateway_destroy(gateway_t *gw);

int gateway_put_object(gateway_t *gw, const gateway_put_request_t *req);

int gateway_get_object(gateway_t *gw,
                        const char *bucket, const char *key,
                        gateway_get_response_t *resp);

int gateway_delete_object(gateway_t *gw,
                           const char *bucket, const char *key);

void gateway_set_ec_policy(gateway_t *gw, ec_policy_t policy);

struct placement_engine;
struct placement_engine *gateway_get_placement(gateway_t *gw);

#endif /* LIGHTFS_GATEWAY_H */
