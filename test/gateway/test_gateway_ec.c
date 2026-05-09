#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway_ec.h"

void test_ec_params(void) {
    printf("Running ec_params test...\n");

    ec_params_t p = ec_params_for_policy(EC_REPLICATION_2X);
    assert(p.data_fragments == 1);
    assert(p.parity_fragments == 1);

    p = ec_params_for_policy(EC_6_PLUS_3);
    assert(p.data_fragments == 6);
    assert(p.parity_fragments == 3);
    assert(p.stripe_size == GATEWAY_MEDIUM_STRIPE_SIZE);

    p = ec_params_for_policy(EC_10_PLUS_4);
    assert(p.data_fragments == 10);
    assert(p.parity_fragments == 4);
    assert(p.stripe_size == GATEWAY_MAX_STRIPE_SIZE);

    printf("ec_params test PASSED\n");
}

void test_ec_encode_decode_small(void) {
    printf("Running ec_encode_decode_small test...\n");

    /* Use K=1, M=1 for simple replication test */
    ec_engine_t *engine = ec_engine_create(1, 1);
    assert(engine != NULL);

    int k = ec_get_k(engine);
    int m = ec_get_m(engine);
    assert(k == 1);
    assert(m == 1);

    int data_size = 4096;
    uint8_t *data = calloc(1, data_size);
    for (int i = 0; i < data_size; i++) data[i] = (uint8_t)(i & 0xFF);

    uint8_t *fragments[2];
    uint64_t frag_sizes[2];
    int rc = ec_encode(engine, data, data_size, fragments, frag_sizes);
    assert(rc == 0);

    /* For K=1, each fragment is full data (replication) */
    assert(frag_sizes[0] == ((data_size + 63) / 64) * 64);
    assert(frag_sizes[1] == ((data_size + 63) / 64) * 64);

    /* Decode with both fragments */
    uint8_t *output = calloc(1, data_size);
    uint64_t decoded_size = data_size;
    int frag_map[2] = {0, 1};

    rc = ec_decode(engine, fragments, frag_sizes, frag_map, 2,
                   output, &decoded_size);
    assert(rc == 0);
    assert(decoded_size > 0);
    assert(memcmp(data, output, data_size) == 0);

    free(fragments[0]);
    free(fragments[1]);
    free(data);
    free(output);
    ec_engine_destroy(engine);
    printf("ec_encode_decode_small test PASSED\n");
}

void test_ec_replication_passthrough(void) {
    printf("Running ec_replication_passthrough test...\n");

    ec_engine_t *engine = ec_engine_create(1, 1);
    assert(engine != NULL);

    int data_size = 1024;
    uint8_t *data = calloc(1, data_size);
    for (int i = 0; i < data_size; i++) data[i] = (uint8_t)(i * 7);

    uint8_t *fragments[2];
    uint64_t frag_sizes[2];
    int rc = ec_encode(engine, data, data_size, fragments, frag_sizes);
    assert(rc == 0);
    assert(frag_sizes[0] == (uint64_t)((data_size + 63) / 64 * 64));
    assert(frag_sizes[1] == (uint64_t)((data_size + 63) / 64 * 64));
    assert(memcmp(data, fragments[0], data_size) == 0);

    for (int i = 0; i < 2; i++) free(fragments[i]);
    free(data);
    ec_engine_destroy(engine);
    printf("ec_replication_passthrough test PASSED\n");
}

int main(void) {
    printf("=== Gateway EC Tests ===\n\n");

    test_ec_params();
    test_ec_encode_decode_small();
    test_ec_replication_passthrough();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
