#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway.h"
#include "lightfs/gateway/gateway_ec.h"
#include "lightfs/gateway/gateway_placement.h"
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

void test_gateway_create_destroy(void) {
  printf("Running gateway_create_destroy test...\n");

  mock_etcd_init();
  etcd_client_t *etcd = etcd_client_create("127.0.0.1", 2379);
  assert(etcd != NULL);
  service_discovery_t *discovery = service_discovery_create(etcd);
  assert(discovery != NULL);

  gateway_config_t config = {
    .node_id = 1,
    .datacenter_id = 0,
    .gateway_port = 8080,
    .default_ec_policy = ERASURE_CODING_6_PLUS_3,
    .default_replication_factor = 2,
    .manifest_cache_size = 1000,
  };

  gateway_t *gateway = gateway_create(&config, discovery, etcd);
  assert(gateway != NULL);

  gateway_destroy(gateway);
  service_discovery_destroy(discovery);
  etcd_client_destroy(etcd);
  printf("gateway_create_destroy test PASSED\n");
}

void test_gateway_put_get_small_object(void) {
  printf("Running gateway_put_get_small_object test...\n");

  mock_etcd_init();
  etcd_client_t *etcd = etcd_client_create("127.0.0.1", 2379);
  assert(etcd != NULL);
  service_discovery_t *discovery = service_discovery_create(etcd);
  assert(discovery != NULL);

  gateway_config_t config = {
    .node_id = 1,
    .datacenter_id = 0,
    .gateway_port = 8080,
    .default_ec_policy = ERASURE_CODING_REPLICATION_2X,
    .default_replication_factor = 2,
    .manifest_cache_size = 100,
  };

  gateway_t *gateway = gateway_create(&config, discovery, etcd);
  assert(gateway != NULL);

  /* Register nodes for placement */
  struct placement_engine *engine = gateway_get_placement(gateway);
  placement_register_node(engine, 1, 0, 0, 1, 1024ULL*1024*1024*1024);
  placement_register_node(engine, 2, 0, 0, 2, 1024ULL*1024*1024*1024);

  char data[512];
  for (int i = 0; i < 512; i++) data[i] = (char)(i & 0xFF);

  gateway_put_request_t request;
  memset(&request, 0, sizeof(request));
  strncpy(request.bucket, "testbucket", sizeof(request.bucket) - 1);
  strncpy(request.key, "hello.txt", sizeof(request.key) - 1);
  request.data = (uint8_t *)data;
  request.size = 512;
  request.has_erasure_coding_override = 0;

  int result = gateway_put_object(gateway, &request);
  assert(result == 0);

  gateway_get_response_t response = {0};
  result =gateway_get_object(gateway, "testbucket", "hello.txt", &response);
  assert(result == 0);
  assert(response.size == 512);

  free(response.data);
  gateway_destroy(gateway);
  service_discovery_destroy(discovery);
  etcd_client_destroy(etcd);
  printf("gateway_put_get_small_object test PASSED\n");
}

void test_gateway_delete_object(void) {
  printf("Running gateway_delete_object test...\n");

  mock_etcd_init();
  etcd_client_t *etcd = etcd_client_create("127.0.0.1", 2379);
  assert(etcd != NULL);
  service_discovery_t *discovery = service_discovery_create(etcd);
  assert(discovery != NULL);

  gateway_config_t config = {
    .node_id = 1,
    .datacenter_id = 0,
    .gateway_port = 8080,
    .default_ec_policy = ERASURE_CODING_REPLICATION_2X,
    .default_replication_factor = 2,
    .manifest_cache_size = 100,
  };

  gateway_t *gateway = gateway_create(&config, discovery, etcd);
  assert(gateway != NULL);

  struct placement_engine *engine = gateway_get_placement(gateway);
  placement_register_node(engine, 1, 0, 0, 1, 1024ULL*1024*1024*1024);
  placement_register_node(engine, 2, 0, 0, 2, 1024ULL*1024*1024*1024);

  char data[64];
  memset(data, 0x42, 64);

  gateway_put_request_t request;
  memset(&request, 0, sizeof(request));
  strncpy(request.bucket, "testbucket", sizeof(request.bucket) - 1);
  strncpy(request.key, "todelete.txt", sizeof(request.key) - 1);
  request.data = (uint8_t *)data;
  request.size = 64;
  request.has_erasure_coding_override = 0;

  int result = gateway_put_object(gateway, &request);
  assert(result == 0);

  result =gateway_delete_object(gateway, "testbucket", "todelete.txt");
  assert(result == 0);

  gateway_get_response_t response = {0};
  result =gateway_get_object(gateway, "testbucket", "todelete.txt", &response);
  assert(result != 0);

  free(response.data);
  gateway_destroy(gateway);
  service_discovery_destroy(discovery);
  etcd_client_destroy(etcd);
  printf("gateway_delete_object test PASSED\n");
}

int main(void) {
  printf("=== Gateway Core Tests ===\n\n");

  test_gateway_create_destroy();
  test_gateway_put_get_small_object();
  test_gateway_delete_object();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
