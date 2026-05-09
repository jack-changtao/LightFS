#include "gateway_ec.h"
#include "lightfs/gateway/gateway_types.h"
#include <stdlib.h>
#include <string.h>

ec_params_t ec_params_for_policy(ec_policy_t policy) {
    ec_params_t p = {0};
    switch (policy) {
    case EC_REPLICATION_2X:
        p.data_fragments = 1;
        p.parity_fragments = 1;
        p.stripe_size = 0;
        break;
    case EC_REPLICATION_3X:
        p.data_fragments = 1;
        p.parity_fragments = 2;
        p.stripe_size = 0;
        break;
    case EC_6_PLUS_3:
        p.data_fragments = 6;
        p.parity_fragments = 3;
        p.stripe_size = GATEWAY_MEDIUM_STRIPE_SIZE;
        break;
    case EC_10_PLUS_4:
        p.data_fragments = 10;
        p.parity_fragments = 4;
        p.stripe_size = GATEWAY_MAX_STRIPE_SIZE;
        break;
    }
    return p;
}

ec_engine_t *ec_engine_create(int k, int m) {
    if (k <= 0 || m < 0 || k + m > GATEWAY_MAX_FRAGMENTS) return NULL;

    ec_engine_t *e = calloc(1, sizeof(ec_engine_t));
    if (!e) return NULL;

    e->k = k;
    e->m = m;

    int n = k + m;
    e->encode_matrix = calloc(n * k, sizeof(uint8_t));
    if (!e->encode_matrix) {
        free(e);
        return NULL;
    }

    for (int i = 0; i < k; i++) {
        e->encode_matrix[i * k + i] = 1;
    }
    for (int i = k; i < n; i++) {
        for (int j = 0; j < k; j++) {
            e->encode_matrix[i * k + j] = (uint8_t)((i - k + 1) * (j + 1) & 0xFF);
        }
    }

    return e;
}

void ec_engine_destroy(ec_engine_t *engine) {
    if (!engine) return;
    free(engine->encode_matrix);
    free(engine);
}

int ec_get_k(ec_engine_t *engine) { return engine ? engine->k : -1; }
int ec_get_m(ec_engine_t *engine) { return engine ? engine->m : -1; }

int ec_encode(ec_engine_t *engine, const uint8_t *data, uint64_t data_size,
              uint8_t **fragments_out, uint64_t *fragment_sizes) {
    if (!engine || !data || !fragments_out || !fragment_sizes) return -1;

    int k = engine->k;
    int n = k + engine->m;

    uint64_t frag_size = (data_size + k - 1) / k;
    frag_size = ((frag_size + 63) / 64) * 64;

    for (int i = 0; i < n; i++) {
        fragments_out[i] = calloc(1, frag_size);
        if (!fragments_out[i]) {
            for (int j = 0; j < i; j++) free(fragments_out[j]);
            return -1;
        }
        fragment_sizes[i] = frag_size;
    }

    for (int i = 0; i < k; i++) {
        uint64_t offset = i * frag_size;
        uint64_t copy = (offset < data_size) ?
                        (data_size - offset < frag_size ? data_size - offset : frag_size) : 0;
        if (copy > 0) memcpy(fragments_out[i], data + offset, copy);
    }

    for (int i = k; i < n; i++) {
        uint8_t *parity = fragments_out[i];
        for (int j = 0; j < k; j++) {
            uint8_t coeff = engine->encode_matrix[i * k + j];
            if (coeff == 0) continue;
            uint8_t *data_frag = fragments_out[j];
            for (uint64_t b = 0; b < frag_size; b++) {
                parity[b] ^= data_frag[b];
            }
        }
    }

    return 0;
}

int ec_decode(ec_engine_t *engine,
              uint8_t **fragments, uint64_t *fragment_sizes,
              int *fragment_map, int fragment_count,
              uint8_t *data_out, uint64_t *data_size_out) {
    if (!engine || !fragments || !fragment_sizes || !fragment_map ||
        !data_out || !data_size_out) return -1;

    int k = engine->k;
    uint64_t frag_size = fragment_sizes[0];

    for (int i = 0; i < k; i++) {
        int found = -1;
        for (int j = 0; j < fragment_count; j++) {
            if (fragment_map[j] == i) { found = j; break; }
        }
        if (found >= 0) {
            memcpy(data_out + i * frag_size, fragments[found], frag_size);
        }
    }

    *data_size_out = frag_size * k;
    return 0;
}
