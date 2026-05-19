#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/cluster_config.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

static int callback_called = 0;
static char callback_last_key[1024] = {0};

static void test_config_callback(const char *key, const char *new_value, void *user_data) {
  callback_called++;
  strncpy(callback_last_key, key, sizeof(callback_last_key) - 1);
  (void)new_value;
  (void)user_data;
}

void test_create_and_get_ec_policy(void) {
  printf("Running create_and_get_ec_policy test...\n");

  mock_etcd_init();

  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  cluster_config_manager_t *manager = cluster_config_create(client);
  assert(manager != NULL);

  ec_policy_config_t policy = {0};
  int result = cluster_config_get_ec_policy(manager, &policy);
  assert(result== 0);
  assert(policy.default_data_k == 10);
  assert(policy.default_parity_m == 4);

  cluster_config_destroy(manager);
  etcd_client_destroy(client);
  printf("create_and_get_ec_policy test PASSED\n");
}

void test_get_nonexistent_bucket(void) {
  printf("Running get_nonexistent_bucket test...\n");

  mock_etcd_init();

  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  cluster_config_manager_t *manager = cluster_config_create(client);
  assert(manager != NULL);

  bucket_config_t config = {0};
  int result = cluster_config_get_bucket(manager, "no-such-bucket", &config);
  assert(result!= 0);

  cluster_config_destroy(manager);
  etcd_client_destroy(client);
  printf("get_nonexistent_bucket test PASSED\n");
}

void test_watch_config_changes(void) {
  printf("Running watch_config_changes test...\n");

  mock_etcd_init();

  etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
  assert(client != NULL);

  cluster_config_manager_t *manager = cluster_config_create(client);
  assert(manager != NULL);

  callback_called = 0;
  int result = cluster_config_watch(manager, test_config_callback, NULL);
  assert(result== 0);

  cluster_config_destroy(manager);
  etcd_client_destroy(client);
  printf("watch_config_changes test PASSED\n");
}

int main(void) {
  printf("=== Cluster Config Tests ===\n\n");

  test_create_and_get_ec_policy();
  test_get_nonexistent_bucket();
  test_watch_config_changes();

  printf("\n=== All tests PASSED ===\n");
  return 0;
}
