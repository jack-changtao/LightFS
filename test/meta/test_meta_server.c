#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/meta/meta_server.h"
#include "lightfs/meta/meta_types.h"

static void fill_manifest(object_manifest_t *m,
                          const char *bucket, const char *key,
                          uint64_t seq) {
    memset(m, 0, sizeof(*m));
    strncpy(m->bucket, bucket, sizeof(m->bucket) - 1);
    strncpy(m->key, key, sizeof(m->key) - 1);
    m->size = 1024;
    m->crc = 0xABCD;
    m->write_seq = seq;
}

void test_create_and_destroy(void) {
    printf("Running create_and_destroy test...\n");

    meta_server_config_t cfg = {
        .server_id = 1,
        .dc_id = 0,
        .split_threshold = 10000,
        .checkpoint_interval_ms = 30000,
    };

    meta_server_t *ms = meta_server_create(&cfg);
    assert(ms != NULL);

    meta_server_destroy(ms);
    printf("create_and_destroy test PASSED\n");
}

void test_push_manifest_batch(void) {
    printf("Running push_manifest_batch test...\n");

    meta_server_config_t cfg = {
        .server_id = 1,
        .dc_id = 0,
        .split_threshold = 10000,
        .checkpoint_interval_ms = 30000,
    };
    meta_server_t *ms = meta_server_create(&cfg);
    assert(ms != NULL);

    manifest_batch_t batch = {0};
    batch.capacity = 2;
    batch.count = 2;
    batch.manifests = calloc(2, sizeof(object_manifest_t));

    fill_manifest(&batch.manifests[0], "testbucket", "file1.txt", 1);
    fill_manifest(&batch.manifests[1], "testbucket", "file2.txt", 2);

    int rc = meta_server_push_manifest_batch(ms, &batch);
    assert(rc == 0);

    free(batch.manifests);
    meta_server_destroy(ms);
    printf("push_manifest_batch test PASSED\n");
}

void test_get_manifest_after_push(void) {
    printf("Running get_manifest_after_push test...\n");

    meta_server_config_t cfg = {
        .server_id = 1,
        .dc_id = 0,
        .split_threshold = 10000,
        .checkpoint_interval_ms = 30000,
    };
    meta_server_t *ms = meta_server_create(&cfg);
    assert(ms != NULL);

    manifest_batch_t batch = {0};
    batch.capacity = 1;
    batch.count = 1;
    batch.manifests = calloc(1, sizeof(object_manifest_t));

    fill_manifest(&batch.manifests[0], "testbucket", "lookup-test.txt", 10);
    batch.manifests[0].crc = 0xDEADBEEF;

    int rc = meta_server_push_manifest_batch(ms, &batch);
    assert(rc == 0);

    object_manifest_t found = {0};
    rc = meta_server_get_manifest(ms, "testbucket", "lookup-test.txt", &found);
    assert(rc == 0);
    assert(found.size == 1024);
    assert(found.crc == 0xDEADBEEF);
    assert(found.write_seq == 1);

    free(batch.manifests);
    meta_server_destroy(ms);
    printf("get_manifest_after_push test PASSED\n");
}

void test_get_manifest_not_found(void) {
    printf("Running get_manifest_not_found test...\n");

    meta_server_config_t cfg = {
        .server_id = 1,
        .dc_id = 0,
        .split_threshold = 10000,
        .checkpoint_interval_ms = 30000,
    };
    meta_server_t *ms = meta_server_create(&cfg);
    assert(ms != NULL);

    object_manifest_t found = {0};
    int rc = meta_server_get_manifest(ms, "no-such-bucket", "no-such-key", &found);
    assert(rc == -1);

    meta_server_destroy(ms);
    printf("get_manifest_not_found test PASSED\n");
}

int main(void) {
    printf("=== Meta Server Tests ===\n\n");

    test_create_and_destroy();
    test_push_manifest_batch();
    test_get_manifest_after_push();
    test_get_manifest_not_found();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
