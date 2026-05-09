#ifndef LIGHTFS_GATEWAY_EC_H
#define LIGHTFS_GATEWAY_EC_H

#include "lightfs/gateway/gateway_types.h"
#include <stdint.h>

typedef struct {
    int data_fragments;
    int parity_fragments;
    int stripe_size;
} ec_params_t;

typedef struct ec_engine ec_engine_t;

ec_params_t ec_params_for_policy(ec_policy_t policy);

ec_engine_t *ec_engine_create(int k, int m);
void ec_engine_destroy(ec_engine_t *engine);

int ec_encode(ec_engine_t *engine, const uint8_t *data, uint64_t data_size,
              uint8_t **fragments_out, uint64_t *fragment_sizes);

int ec_decode(ec_engine_t *engine,
              uint8_t **fragments, uint64_t *fragment_sizes,
              int *fragment_map, int fragment_count,
              uint8_t *data_out, uint64_t *data_size_out);

int ec_get_k(ec_engine_t *engine);
int ec_get_m(ec_engine_t *engine);

#endif /* LIGHTFS_GATEWAY_EC_H */
