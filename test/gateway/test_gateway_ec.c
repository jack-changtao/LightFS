#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway_ec.h"

void test_ec_params(void) {
    printf("Running ec_params test...\n");

    erasure_coding_params_t params = erasure_coding_params_for_policy(ERASURE_CODING_REPLICATION_2X);
    assert(params.data_fragments == 1);
    assert(params.parity_fragments == 1);

    params = erasure_coding_params_for_policy(ERASURE_CODING_6_PLUS_3);
    assert(params.data_fragments == 6);
    assert(params.parity_fragments == 3);
    assert(params.stripe_size == GATEWAY_MEDIUM_STRIPE_SIZE);

    params = erasure_coding_params_for_policy(ERASURE_CODING_10_PLUS_4);
    assert(params.data_fragments == 10);
    assert(params.parity_fragments == 4);
    assert(params.stripe_size == GATEWAY_MAX_STRIPE_SIZE);

    printf("ec_params test PASSED\n");
}

void test_erasure_coding_encode_decode_small(void) {
    printf("Running erasure_coding_encode_decode_small test...\n");

    /* Use K=1, M=1 for simple replication test */
    erasure_coding_engine_t *engine = erasure_coding_engine_create(1, 1);
    assert(engine != NULL);

    int k = erasure_coding_get_k(engine);
    int m = erasure_coding_get_m(engine);
    assert(k == 1);
    assert(m == 1);

    int data_size = 4096;
    uint8_t *data = calloc(1, data_size);
    for (int i = 0; i < data_size; i++) data[i] = (uint8_t)(i & 0xFF);

    uint8_t *fragments[2];
    uint64_t frag_sizes[2];
    int result = erasure_coding_encode(engine, data, data_size, fragments, frag_sizes);
    assert(result == 0);

    /* For K=1, each fragment is full data (replication) */
    assert(frag_sizes[0] == ((data_size + 63) / 64) * 64);
    assert(frag_sizes[1] == ((data_size + 63) / 64) * 64);

    /* Decode with both fragments */
    uint8_t *output = calloc(1, data_size);
    uint64_t decoded_size = data_size;
    int frag_map[2] = {0, 1};

    result = erasure_coding_decode(engine, fragments, frag_sizes, frag_map, 2,
                   output, &decoded_size);
    assert(result == 0);
    assert(decoded_size > 0);
    assert(memcmp(data, output, data_size) == 0);

    free(fragments[0]);
    free(fragments[1]);
    free(data);
    free(output);
    erasure_coding_engine_destroy(engine);
    printf("erasure_coding_encode_decode_small test PASSED\n");
}

void test_ec_replication_passthrough(void) {
    printf("Running ec_replication_passthrough test...\n");

    erasure_coding_engine_t *engine = erasure_coding_engine_create(1, 1);
    assert(engine != NULL);

    int data_size = 1024;
    uint8_t *data = calloc(1, data_size);
    for (int i = 0; i < data_size; i++) data[i] = (uint8_t)(i * 7);

    uint8_t *fragments[2];
    uint64_t frag_sizes[2];
    int result = erasure_coding_encode(engine, data, data_size, fragments, frag_sizes);
    assert(result == 0);
    assert(frag_sizes[0] == (uint64_t)((data_size + 63) / 64 * 64));
    assert(frag_sizes[1] == (uint64_t)((data_size + 63) / 64 * 64));
    assert(memcmp(data, fragments[0], data_size) == 0);

    for (int i = 0; i < 2; i++) free(fragments[i]);
    free(data);
    erasure_coding_engine_destroy(engine);
    printf("ec_replication_passthrough test PASSED\n");
}

int main(void) {
    printf("=== Gateway EC Tests ===\n\n");

    test_ec_params();
    test_erasure_coding_encode_decode_small();
    test_ec_replication_passthrough();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
