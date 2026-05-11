#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/meta/meta_shard.h"
#include "lightfs/meta/meta_types.h"

static void fill_manifest(object_manifest_t *manifest,
                          const char *bucket, const char *key,
                          uint64_t seq) {
    memset(manifest, 0, sizeof(*manifest));
    strncpy(manifest->bucket, bucket, sizeof(manifest->bucket) - 1);
    strncpy(manifest->key, key, sizeof(manifest->key) - 1);
    manifest->size = 1024;
    manifest->checksum = 0xABCD;
    manifest->write_sequence = seq;
}

void test_insert_and_lookup(void) {
    printf("Running insert_and_lookup test...\n");

    meta_shard_t *shard = meta_shard_create(1, 0, "testbucket");
    assert(shard != NULL);

    object_manifest_t manifest;
    fill_manifest(&manifest, "testbucket", "file.txt", 1);
    int result = meta_shard_insert(shard, &manifest);
    assert(result== 0);

    object_manifest_t found = {0};
    result =meta_shard_lookup(shard, "testbucket", "file.txt", &found);
    assert(result== 0);
    assert(found.size == 1024);
    assert(found.write_sequence == 1);

    meta_shard_destroy(shard);
    printf("insert_and_lookup test PASSED\n");
}

void test_bulk_insert_and_list(void) {
    printf("Running bulk_insert_and_list test...\n");

    meta_shard_t *shard = meta_shard_create(1, 0, "testbucket");
    assert(shard != NULL);

    for (int i = 0; i < 50; i++) {
        char key[32];
        snprintf(key, sizeof(key), "file%03d.txt", i);
        object_manifest_t manifest;
        fill_manifest(&manifest, "testbucket", key, (uint64_t)i + 1);
        meta_shard_insert(shard, &manifest);
    }

    char *keys[100];
    for (int i = 0; i < 100; i++) keys[i] = calloc(1, 64);
    int count = 0;

    int result = meta_shard_list(shard, "testbucket", "file0", "", 10, keys, &count);
    assert(result== 0);
    assert(count == 10);
    assert(strcmp(keys[0], "file000.txt") == 0);

    for (int i = 0; i < 100; i++) free(keys[i]);
    meta_shard_destroy(shard);
    printf("bulk_insert_and_list test PASSED\n");
}

void test_split_blocks_loading_child(void) {
    printf("Running split_blocks_loading_child test...\n");

    meta_shard_t *shard = meta_shard_create(1, 0, "testbucket");
    assert(shard != NULL);

    for (int i = 0; i < 10; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%02d", i);
        object_manifest_t manifest;
        fill_manifest(&manifest, "testbucket", key, (uint64_t)i + 1);
        meta_shard_insert(shard, &manifest);
    }

    meta_shard_t *child = meta_shard_split(shard, 2);
    assert(child != NULL);

    meta_shard_t *child2 = meta_shard_split(shard, 3);
    assert(child2 == NULL);

    meta_shard_destroy(child);
    meta_shard_child_activated(shard);
    child2 = meta_shard_split(shard, 3);
    assert(child2 != NULL);

    meta_shard_destroy(shard);
    meta_shard_destroy(child2);
    printf("split_blocks_loading_child test PASSED\n");
}

void test_delete_and_verify_missing(void) {
    printf("Running delete_and_verify_missing test...\n");

    meta_shard_t *shard = meta_shard_create(1, 0, "testbucket");
    assert(shard != NULL);

    object_manifest_t manifest;
    fill_manifest(&manifest, "testbucket", "delete-me.txt", 1);
    meta_shard_insert(shard, &manifest);

    int result = meta_shard_delete(shard, "testbucket", "delete-me.txt");
    assert(result== 0);

    object_manifest_t found = {0};
    result =meta_shard_lookup(shard, "testbucket", "delete-me.txt", &found);
    assert(result== -1);

    meta_shard_destroy(shard);
    printf("delete_and_verify_missing test PASSED\n");
}

int main(void) {
    printf("=== Meta Shard Tests ===\n\n");

    test_insert_and_lookup();
    test_bulk_insert_and_list();
    test_split_blocks_loading_child();
    test_delete_and_verify_missing();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
