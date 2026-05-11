#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gateway_manifest_cache.h"

void test_cache_insert_lookup(void) {
    printf("Running cache_insert_lookup test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    strncpy(manifest.bucket, "testbucket", sizeof(manifest.bucket) - 1);
    strncpy(manifest.key, "testkey", sizeof(manifest.key) - 1);
    manifest.size = 4096;
    manifest.write_sequence = 42;
    manifest.checksum = 0xCAFE;

    manifest_cache_insert(cache, "testbucket", "testkey", &manifest);

    object_manifest_t found = {0};
    int result = manifest_cache_lookup(cache, "testbucket", "testkey", &found);
    assert(result== 0);
    assert(found.size == 4096);
    assert(found.write_sequence == 42);
    assert(found.checksum == 0xCAFE);

    manifest_cache_destroy(cache);
    printf("cache_insert_lookup test PASSED\n");
}

void test_cache_miss(void) {
    printf("Running cache_miss test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t found = {0};
    int result = manifest_cache_lookup(cache, "nope", "nope", &found);
    assert(result== -1);

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
        object_manifest_t manifest;
        memset(&manifest, 0, sizeof(manifest));
        strncpy(manifest.bucket, "b", sizeof(manifest.bucket) - 1);
        strncpy(manifest.key, key, sizeof(manifest.key) - 1);
        manifest.size = (uint64_t)i;
        manifest_cache_insert(cache, "b", key, &manifest);
    }

    int size = manifest_cache_size(cache);
    assert(size <= 10);

    object_manifest_t found = {0};
    int result = manifest_cache_lookup(cache, "b", "key019", &found);
    assert(result== 0);

    manifest_cache_destroy(cache);
    printf("cache_eviction test PASSED\n");
}

void test_cache_invalidate(void) {
    printf("Running cache_invalidate test...\n");

    manifest_cache_t *cache = manifest_cache_create(100);
    assert(cache != NULL);

    object_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    strncpy(manifest.bucket, "b", sizeof(manifest.bucket) - 1);
    strncpy(manifest.key, "k", sizeof(manifest.key) - 1);
    manifest_cache_insert(cache, "b", "k", &manifest);

    manifest_cache_invalidate(cache, "b", "k");

    object_manifest_t found = {0};
    int result = manifest_cache_lookup(cache, "b", "k", &found);
    assert(result== -1);

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
