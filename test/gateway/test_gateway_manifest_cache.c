#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gateway_manifest_cache.h"

void test_cache_insert_lookup(void) {
    printf("Running cache_insert_lookup test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.bucket, "testbucket", sizeof(m.bucket) - 1);
    strncpy(m.key, "testkey", sizeof(m.key) - 1);
    m.size = 4096;
    m.write_seq = 42;
    m.crc = 0xCAFE;

    manifest_cache_insert(cache, "testbucket", "testkey", &m);

    object_manifest_t found = {0};
    int rc = manifest_cache_lookup(cache, "testbucket", "testkey", &found);
    assert(rc == 0);
    assert(found.size == 4096);
    assert(found.write_seq == 42);
    assert(found.crc == 0xCAFE);

    manifest_cache_destroy(cache);
    printf("cache_insert_lookup test PASSED\n");
}

void test_cache_miss(void) {
    printf("Running cache_miss test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t found = {0};
    int rc = manifest_cache_lookup(cache, "nope", "nope", &found);
    assert(rc == -1);

    manifest_cache_destroy(cache);
    printf("cache_miss test PASSED\n");
}

void test_cache_eviction(void) {
    printf("Running cache_eviction test...\n");

    manifest_cache_t *cache = manifest_cache_create(10);
    assert(cache != NULL);

    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%03d", i);
        object_manifest_t m;
        memset(&m, 0, sizeof(m));
        strncpy(m.bucket, "b", sizeof(m.bucket) - 1);
        strncpy(m.key, key, sizeof(m.key) - 1);
        m.size = (uint64_t)i;
        manifest_cache_insert(cache, "b", key, &m);
    }

    int size = manifest_cache_size(cache);
    assert(size <= 10);

    object_manifest_t found = {0};
    int rc = manifest_cache_lookup(cache, "b", "key019", &found);
    assert(rc == 0);

    manifest_cache_destroy(cache);
    printf("cache_eviction test PASSED\n");
}

void test_cache_invalidate(void) {
    printf("Running cache_invalidate test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.bucket, "b", sizeof(m.bucket) - 1);
    strncpy(m.key, "k", sizeof(m.key) - 1);
    manifest_cache_insert(cache, "b", "k", &m);

    manifest_cache_invalidate(cache, "b", "k");

    object_manifest_t found = {0};
    int rc = manifest_cache_lookup(cache, "b", "k", &found);
    assert(rc == -1);

    manifest_cache_destroy(cache);
    printf("cache_invalidate test PASSED\n");
}

int main(void) {
    printf("=== Manifest Cache Tests ===\n\n");

    test_cache_insert_lookup();
    test_cache_miss();
    test_cache_eviction();
    test_cache_invalidate();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
