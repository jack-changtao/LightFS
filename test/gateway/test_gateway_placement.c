#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway_placement.h"
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

void test_placement_create_destroy(void) {
  printf("Running placement_create_destroy test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);
  service_discovery_t *discovery = service_discovery_create(client);
  assert(discovery != NULL);

  placement_engine_t *engine = placement_engine_create(discovery);
  assert(engine != NULL);
  placement_engine_destroy(engine);
  service_discovery_destroy(discovery);
  etcd_client_destroy(client);

  printf("placement_create_destroy test PASSED\n");
}

void test_placement_select_targets_6_3(void) {
  printf("Running placement_select_targets_6_3 test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);
  service_discovery_t *discovery = service_discovery_create(client);
  assert(discovery != NULL);

  placement_engine_t *engine = placement_engine_create(discovery);
  assert(engine != NULL);

  for (int i = 1; i <= 12; i++) {
    uint32_t dc = (uint32_t)((i - 1) / 6);
    uint32_t rack = (uint32_t)(((i - 1) % 6) / 3);
    uint32_t host = (uint32_t)i;
    placement_register_node(engine, (uint32_t)i, dc, rack, host,
                1024ULL * 1024 * 1024 * 1024);
  }

  placement_target_t targets[9];
  int count = placement_select_targets(engine, 6, 3, targets, 9);
  assert(count == 9);

  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      assert(targets[i].node_id != targets[j].node_id);
    }
  }

  placement_engine_destroy(engine);
  service_discovery_destroy(discovery);
  etcd_client_destroy(client);

  printf("placement_select_targets_6_3 test PASSED\n");
}

void test_placement_insufficient_targets(void) {
  printf("Running placement_insufficient_targets test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);
  service_discovery_t *discovery = service_discovery_create(client);
  assert(discovery != NULL);

  placement_engine_t *engine = placement_engine_create(discovery);
  assert(engine != NULL);

  placement_register_node(engine, 1, 0, 0, 1, 1024ULL * 1024 * 1024 * 1024);
  placement_register_node(engine, 2, 0, 1, 2, 1024ULL * 1024 * 1024 * 1024);

  placement_target_t targets[14];
  int count = placement_select_targets(engine, 10, 4, targets, 14);
  assert(count == -1);

  placement_engine_destroy(engine);
  service_discovery_destroy(discovery);
  etcd_client_destroy(client);

  printf("placement_insufficient_targets test PASSED\n");
}

int main(void) {
  printf("=== Gateway Placement Tests ===\n\n");

  test_placement_create_destroy();
  test_placement_select_targets_6_3();
  test_placement_insufficient_targets();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
