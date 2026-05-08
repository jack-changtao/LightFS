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
        object_manifest_t m;
        memset(&m, 0, sizeof(m));
        strncpy(m.bucket, "testbucket", sizeof(m.bucket) - 1);
        strncpy(m.key, key, sizeof(m.key) - 1);
        m.size = (uint64_t)(i * 100);
        m.crc = (uint32_t)i;
        m.write_seq = (uint64_t)(i + 1);
        meta_shard_insert(shard, &m);
    }

    uint64_t checkpoint_id = 0;
    int n = meta_checkpoint_write(shard, 1, &checkpoint_id);
    assert(n > 0);

    meta_shard_t *shard2 = meta_shard_create(1, 0, "testbucket");
    assert(shard2 != NULL);

    int rc = meta_checkpoint_read(shard2, checkpoint_id);
    assert(rc == 0);

    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof(key), "obj%03d", i);
        object_manifest_t found = {0};
        rc = meta_shard_lookup(shard2, "testbucket", key, &found);
        assert(rc == 0);
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
