#ifndef LIGHTFS_GATEWAY_EC_INTERNAL_H
#define LIGHTFS_GATEWAY_EC_INTERNAL_H

#include "lightfs/gateway/gateway_ec.h"

struct erasure_coding_engine {
    int data_fragment_count;
    int parity_fragment_count;
    uint8_t *encode_matrix;
};

#endif /* LIGHTFS_GATEWAY_EC_INTERNAL_H */
