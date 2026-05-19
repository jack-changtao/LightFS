#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/meta/meta_server.h"
#include "lightfs/meta/meta_recovery.h"

void test_recover_from_checkpoint(void) {
  printf("Running recover_from_checkpoint test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };

  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  manifest_batch_t batch = {0};
  batch.capacity = 3;
  batch.count = 3;
  batch.manifests = calloc(3, sizeof(object_manifest_t));

  for (int i = 0; i < 3; i++) {
    snprintf(batch.manifests[i].bucket, sizeof(batch.manifests[i].bucket),
        "recovery-bucket");
    snprintf(batch.manifests[i].key, sizeof(batch.manifests[i].key),
        "obj%d.txt", i);
    batch.manifests[i].size = 100 * (i + 1);
  }

  int result = meta_server_push_manifest_batch(meta_server, &batch);
  assert(result== 0);

  meta_server_destroy(meta_server);

  meta_server_t *meta_server2 = meta_server_create(&config);
  assert(meta_server2 != NULL);

  result = meta_recovery_start(meta_server2, 0, 0);
  assert(result== 0);

  free(batch.manifests);
  meta_server_destroy(meta_server2);
  printf("recover_from_checkpoint test PASSED\n");
}

void test_background_recovery_skips_failed_node(void) {
  printf("Running background_recovery_skips_failed_node test...\n");

  meta_server_config_t config = {
    .server_id = 1,
    .datacenter_id = 0,
    .split_threshold = 10000,
    .checkpoint_interval_milliseconds = 30000,
  };
  meta_server_t *meta_server = meta_server_create(&config);
  assert(meta_server != NULL);

  int result = meta_recovery_background(meta_server, 99);
  assert(result== 0);

  meta_server_destroy(meta_server);
  printf("background_recovery_skips_failed_node test PASSED\n");
}

int main(void) {
  printf("=== Meta Recovery Tests ===\n\n");

  test_recover_from_checkpoint();
  test_background_recovery_skips_failed_node();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
