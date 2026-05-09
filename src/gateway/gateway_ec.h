#ifndef LIGHTFS_GATEWAY_EC_INTERNAL_H
#define LIGHTFS_GATEWAY_EC_INTERNAL_H

#include "lightfs/gateway/gateway_ec.h"

struct ec_engine {
    int k;
    int m;
    uint8_t *encode_matrix;
};

#endif /* LIGHTFS_GATEWAY_EC_INTERNAL_H */
