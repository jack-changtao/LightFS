#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway_replication.h"

void test_add_peer(void) {
  printf("Running add_peer test...\n");

  replication_engine_t *replication = replication_engine_create();
  assert(replication != NULL);

  replication_peer_t peer = {
    .peer_dc_id = 2,
    .peer_gateway_host = "10.0.2.1",
    .peer_gateway_port = 8080,
  };
  int result = replication_add_peer(replication, &peer);
  assert(result== 0);

  replication_engine_destroy(replication);
  printf("add_peer test PASSED\n");
}

void test_enqueue_replication(void) {
  printf("Running enqueue_replication test...\n");

  replication_engine_t *replication = replication_engine_create();
  assert(replication != NULL);

  replication_peer_t peer = {.peer_dc_id = 2, .peer_gateway_host = "10.0.2.1",
                .peer_gateway_port = 8080};
  replication_add_peer(replication, &peer);

  object_manifest_t manifest;
  memset(&manifest, 0, sizeof(manifest));
  strncpy(manifest.bucket, "geo-bucket", sizeof(manifest.bucket) - 1);
  strncpy(manifest.key, "obj1", sizeof(manifest.key) - 1);
  manifest.size = 2048;
  manifest.write_sequence = 100;
  manifest.datacenter_id = 1;

  int result = replication_enqueue(replication, &manifest, NULL, manifest.size);
  assert(result== 0);
  assert(replication_pending_count(replication) == 1);

  replication_engine_destroy(replication);
  printf("enqueue_replication test PASSED\n");
}

void test_lww_conflict_resolution(void) {
  printf("Running lww_conflict_resolution test...\n");

  int result = replication_resolve_conflict(200, 1, 100, 2);
  assert(result== 1);

  result =replication_resolve_conflict(50, 1, 100, 2);
  assert(result== 0);

  result =replication_resolve_conflict(100, 2, 100, 1);
  assert(result== 1);

  result =replication_resolve_conflict(100, 1, 100, 2);
  assert(result== 0);

  printf("lww_conflict_resolution test PASSED\n");
}

int main(void) {
  printf("=== Gateway Replication Tests ===\n\n");

  test_add_peer();
  test_enqueue_replication();
  test_lww_conflict_resolution();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
