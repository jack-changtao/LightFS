#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

void test_create_and_destroy(void) {
  printf("Running create_and_destroy test...\n");

  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);
  etcd_client_destroy(client);

  printf("create_and_destroy test PASSED\n");
}

void test_grant_and_revoke_lease(void) {
  printf("Running grant_and_revoke_lease test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  etcd_lease_t lease = {0};
  int result = etcd_lease_grant(client, 10, &lease);
  assert(result == 0);
  assert(lease.id > 0);
  assert(lease.ttl == 10);

  result = etcd_lease_revoke(client, lease.id);
  assert(result == 0);

  etcd_client_destroy(client);
  printf("grant_and_revoke_lease test PASSED\n");
}

void test_put_and_get_kv(void) {
  printf("Running put_and_get_kv test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  int result = etcd_key_value_put(client, "/test/key1", "hello", 0);
  assert(result == 0);

  etcd_key_value_response_t response = {0};
  result = etcd_key_value_get(client, "/test/key1", &response);
  assert(result == 0);
  assert(response.value != NULL);
  assert(strcmp(response.value, "hello") == 0);

  free(response.key);
  free(response.value);
  etcd_client_destroy(client);
  printf("put_and_get_kv test PASSED\n");
}

void test_get_nonexistent_key(void) {
  printf("Running get_nonexistent_key test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  etcd_key_value_response_t response = {0};
  int result = etcd_key_value_get(client, "/nonexistent", &response);
  assert(result != 0);

  etcd_client_destroy(client);
  printf("get_nonexistent_key test PASSED\n");
}

void test_delete_key(void) {
  printf("Running delete_key test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  etcd_key_value_put(client, "/test/del_key", "temp", 0);
  int result = etcd_key_value_delete(client, "/test/del_key");
  assert(result == 0);

  etcd_key_value_response_t response = {0};
  result = etcd_key_value_get(client, "/test/del_key", &response);
  assert(result != 0);

  etcd_client_destroy(client);
  printf("delete_key test PASSED\n");
}

void test_put_with_lease(void) {
  printf("Running put_with_lease test...\n");

  mock_etcd_init();
  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  etcd_lease_t lease = {0};
  etcd_lease_grant(client, 10, &lease);

  int result = etcd_key_value_put(client, "/test/leased_key", "leased_value", lease.id);
  assert(result == 0);

  etcd_key_value_response_t response = {0};
  result = etcd_key_value_get(client, "/test/leased_key", &response);
  assert(result == 0);
  assert(strcmp(response.value, "leased_value") == 0);

  free(response.key);
  free(response.value);
  etcd_client_destroy(client);
  printf("put_with_lease test PASSED\n");
}

int main(void) {
  printf("=== etcd Client Tests ===\n\n");

  test_create_and_destroy();
  test_grant_and_revoke_lease();
  test_put_and_get_kv();
  test_get_nonexistent_key();
  test_delete_key();
  test_put_with_lease();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
