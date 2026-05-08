#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

void test_create_and_destroy(void) {
    printf("Running create_and_destroy test...\n");

    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);
    etcd_client_destroy(c);

    printf("create_and_destroy test PASSED\n");
}

void test_grant_and_revoke_lease(void) {
    printf("Running grant_and_revoke_lease test...\n");

    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);

    etcd_lease_t lease = {0};
    int rc = etcd_lease_grant(c, 10, &lease);
    assert(rc == 0);
    assert(lease.id > 0);
    assert(lease.ttl == 10);

    rc = etcd_lease_revoke(c, lease.id);
    assert(rc == 0);

    etcd_client_destroy(c);
    printf("grant_and_revoke_lease test PASSED\n");
}

void test_put_and_get_kv(void) {
    printf("Running put_and_get_kv test...\n");

    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);

    int rc = etcd_kv_put(c, "/test/key1", "hello", 0);
    assert(rc == 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/key1", &resp);
    assert(rc == 0);
    assert(resp.value != NULL);
    assert(strcmp(resp.value, "hello") == 0);

    free(resp.key);
    free(resp.value);
    etcd_client_destroy(c);
    printf("put_and_get_kv test PASSED\n");
}

void test_get_nonexistent_key(void) {
    printf("Running get_nonexistent_key test...\n");

    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);

    etcd_kv_response_t resp = {0};
    int rc = etcd_kv_get(c, "/nonexistent", &resp);
    assert(rc != 0);

    etcd_client_destroy(c);
    printf("get_nonexistent_key test PASSED\n");
}

void test_delete_key(void) {
    printf("Running delete_key test...\n");

    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);

    etcd_kv_put(c, "/test/del_key", "temp", 0);
    int rc = etcd_kv_delete(c, "/test/del_key");
    assert(rc == 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/del_key", &resp);
    assert(rc != 0);

    etcd_client_destroy(c);
    printf("delete_key test PASSED\n");
}

void test_put_with_lease(void) {
    printf("Running put_with_lease test...\n");

    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    assert(c != NULL);

    etcd_lease_t lease = {0};
    etcd_lease_grant(c, 10, &lease);

    int rc = etcd_kv_put(c, "/test/leased_key", "leased_value", lease.id);
    assert(rc == 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/leased_key", &resp);
    assert(rc == 0);
    assert(strcmp(resp.value, "leased_value") == 0);

    free(resp.key);
    free(resp.value);
    etcd_client_destroy(c);
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
