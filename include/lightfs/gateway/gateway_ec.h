#ifndef LIGHTFS_GATEWAY_EC_H
#define LIGHTFS_GATEWAY_EC_H

#include "lightfs/gateway/gateway_types.h"
#include <stdint.h>

typedef struct {
    int data_fragments;
    int parity_fragments;
    int stripe_size;
} erasure_coding_params_t;

typedef struct erasure_coding_engine erasure_coding_engine_t;

erasure_coding_params_t erasure_coding_params_for_policy(erasure_coding_policy_t policy);

erasure_coding_engine_t *erasure_coding_engine_create(int data_fragment_count, int parity_fragment_count);
void erasure_coding_engine_destroy(erasure_coding_engine_t *engine);

int erasure_coding_encode(erasure_coding_engine_t *engine, const uint8_t *data, uint64_t data_size,
              uint8_t **fragments_out, uint64_t *fragment_sizes);

int erasure_coding_decode(erasure_coding_engine_t *engine,
              uint8_t **fragments, uint64_t *fragment_sizes,
              int *fragment_map, int fragment_count,
              uint8_t *data_out, uint64_t *data_size_out);

int erasure_coding_get_data_fragment_count(erasure_coding_engine_t *engine);
int erasure_coding_get_parity_fragment_count(erasure_coding_engine_t *engine);

#endif /* LIGHTFS_GATEWAY_EC_H */
