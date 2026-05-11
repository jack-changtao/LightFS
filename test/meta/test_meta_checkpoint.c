#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/meta/meta_shard.h"
#include "meta_checkpoint.h"

void test_serialize_and_deserialize(void) {
    printf("Running serialize_and_deserialize test...\n");

    meta_shard_t *shard = meta_shard_create(1, 0, "testbucket");
    assert(shard != NULL);

    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "obj%03d", i);
        object_manifest_t manifest;
        memset(&manifest, 0, sizeof(manifest));
        strncpy(manifest.bucket, "testbucket", sizeof(manifest.bucket) - 1);
        strncpy(manifest.key, key, sizeof(manifest.key) - 1);
        manifest.size = (uint64_t)(i * 100);
        manifest.checksum = (uint32_t)i;
        manifest.write_sequence = (uint64_t)(i + 1);
        meta_shard_insert(shard, &manifest);
    }

    uint64_t checkpoint_id = 0;
    int written = meta_checkpoint_write(shard, 1, &checkpoint_id);
    assert(written > 0);

    meta_shard_t *shard2 = meta_shard_create(1, 0, "testbucket");
    assert(shard2 != NULL);

    int result = meta_checkpoint_read(shard2, checkpoint_id);
    assert(result== 0);

    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "obj%03d", i);
        object_manifest_t found = {0};
        result =meta_shard_lookup(shard2, "testbucket", key, &found);
        assert(result== 0);
        assert(found.size == (uint64_t)(i * 100));
    }

    meta_shard_destroy(shard);
    meta_shard_destroy(shard2);
    printf("serialize_and_deserialize test PASSED\n");
}

int main(void) {
    printf("=== Meta Checkpoint Tests ===\n\n");

    test_serialize_and_deserialize();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
