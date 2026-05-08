#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/cluster_node.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

void test_join_and_get_info(void) {
    printf("Running join_and_get_info test...\n");

    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 1,
        .dc_id = 0,
        .host = "10.0.0.1",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 4,
        .total_disk_bytes = 4ULL * 1024 * 1024 * 1024 * 1024,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    assert(node != NULL);

    const cluster_node_info_t *info = cluster_node_get_info(node);
    assert(info != NULL);
    assert(info->node_id == 1);
    assert(info->dc_id == 0);
    assert(info->gateway_port == 8080);
    assert(info->status == NODE_ACTIVE);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
    printf("join_and_get_info test PASSED\n");
}

void test_heartbeat_succeeds(void) {
    printf("Running heartbeat_succeeds test...\n");

    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 2,
        .dc_id = 0,
        .host = "10.0.0.2",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 2,
        .total_disk_bytes = 2ULL * 1024 * 1024 * 1024 * 1024,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    assert(node != NULL);

    int rc = cluster_node_heartbeat(node);
    assert(rc == 0);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
    printf("heartbeat_succeeds test PASSED\n");
}

void test_leave_sets_draining(void) {
    printf("Running leave_sets_draining test...\n");

    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 3,
        .dc_id = 0,
        .host = "10.0.0.3",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 1,
        .total_disk_bytes = 1024 * 1024 * 1024 * 1024ULL,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    assert(node != NULL);

    int rc = cluster_node_leave(node);
    assert(rc == 0);

    const cluster_node_info_t *info = cluster_node_get_info(node);
    assert(info != NULL);
    assert(info->status == NODE_DRAINING);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
    printf("leave_sets_draining test PASSED\n");
}

int main(void) {
    printf("=== Cluster Node Tests ===\n\n");

    test_join_and_get_info();
    test_heartbeat_succeeds();
    test_leave_sets_draining();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
