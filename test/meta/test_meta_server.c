#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/meta/meta_server.h"
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

void test_create_and_destroy(void) {
  printf("Running create_and_destroy test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };

  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  meta_server_destroy(meta_server);
  printf("create_and_destroy test PASSED\n");
}

void test_push_manifest_batch(void) {
  printf("Running push_manifest_batch test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };
  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  manifest_batch_t batch = {0};
  batch.capacity = 2;
  batch.count = 2;
  batch.manifests = calloc(2, sizeof(object_manifest_t));

  fill_manifest(&batch.manifests[0], "testbucket", "file1.txt", 1);
  fill_manifest(&batch.manifests[1], "testbucket", "file2.txt", 2);

  int result = meta_server_push_manifest_batch(meta_server, &batch);
  assert(result== 0);

  free(batch.manifests);
  meta_server_destroy(meta_server);
  printf("push_manifest_batch test PASSED\n");
}

void test_get_manifest_after_push(void) {
  printf("Running get_manifest_after_push test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };
  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  manifest_batch_t batch = {0};
  batch.capacity = 1;
  batch.count = 1;
  batch.manifests = calloc(1, sizeof(object_manifest_t));

  fill_manifest(&batch.manifests[0], "testbucket", "lookup-test.txt", 10);
  batch.manifests[0].checksum = 0xDEADBEEF;

  int result = meta_server_push_manifest_batch(meta_server, &batch);
  assert(result== 0);

  object_manifest_t found = {0};
  result = meta_server_get_manifest(meta_server, "testbucket", "lookup-test.txt", &found);
  assert(result== 0);
  assert(found.size == 1024);
  assert(found.checksum == 0xDEADBEEF);
  assert(found.write_sequence == 1);

  free(batch.manifests);
  meta_server_destroy(meta_server);
  printf("get_manifest_after_push test PASSED\n");
}

void test_get_manifest_not_found(void) {
  printf("Running get_manifest_not_found test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };
  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  object_manifest_t found = {0};
  int result = meta_server_get_manifest(meta_server, "no-such-bucket", "no-such-key", &found);
  assert(result== -1);

  meta_server_destroy(meta_server);
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
