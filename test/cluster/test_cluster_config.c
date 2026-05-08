#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/cluster_config.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

static int cb_called = 0;
static char cb_last_key[1024] = {0};

static void test_config_cb(const char *key, const char *new_value, void *ctx) {
    cb_called++;
    strncpy(cb_last_key, key, sizeof(cb_last_key) - 1);
    (void)new_value;
    (void)ctx;
}

void test_create_and_get_ec_policy(void) {
    printf("Running create_and_get_ec_policy test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    assert(mgr != NULL);

    ec_policy_config_t policy = {0};
    int rc = cluster_config_get_ec_policy(mgr, &policy);
    assert(rc == 0);
    assert(policy.default_data_k == 10);
    assert(policy.default_parity_m == 4);

    cluster_config_destroy(mgr);
    etcd_client_destroy(client);
    printf("create_and_get_ec_policy test PASSED\n");
}

void test_get_nonexistent_bucket(void) {
    printf("Running get_nonexistent_bucket test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    assert(mgr != NULL);

    bucket_config_t cfg = {0};
    int rc = cluster_config_get_bucket(mgr, "no-such-bucket", &cfg);
    assert(rc != 0);

    cluster_config_destroy(mgr);
    etcd_client_destroy(client);
    printf("get_nonexistent_bucket test PASSED\n");
}

void test_watch_config_changes(void) {
    printf("Running watch_config_changes test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    assert(mgr != NULL);

    cb_called = 0;
    int rc = cluster_config_watch(mgr, test_config_cb, NULL);
    assert(rc == 0);

    cluster_config_destroy(mgr);
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
