#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/cluster_node.h"
#include "lightfs/cluster/etcd_client.h"
#include "mock_etcd.h"

static int topology_callback_count = 0;

static void test_topology_callback(void *user_data) {
    topology_callback_count++;
    (void)user_data;
}

void test_create_returns_non_null(void) {
    printf("Running create_returns_non_null test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    service_discovery_t *discovery = service_discovery_create(client);
    assert(discovery != NULL);

    service_discovery_destroy(discovery);
    etcd_client_destroy(client);
    printf("create_returns_non_null test PASSED\n");
}

void test_find_joined_gateway(void) {
    printf("Running find_joined_gateway test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    cluster_node_config_t config = {
        .node_id = 10,
        .datacenter_id = 0,
        .host = "10.0.1.10",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 2,
        .total_disk_bytes = 2ULL * 1024 * 1024 * 1024 * 1024,
    };
    cluster_node_t *node = cluster_node_join(client, &config);
    assert(node != NULL);

    service_discovery_t *discovery = service_discovery_create(client);
    assert(discovery != NULL);

    service_endpoint_t endpoints[10];
    int count = service_discovery_get_gateways(discovery, endpoints, 10);
    assert(count >= 1);

    cluster_node_destroy(node);
    service_discovery_destroy(discovery);
    etcd_client_destroy(client);
    printf("find_joined_gateway test PASSED\n");
}

void test_topology_watch_callback(void) {
    printf("Running topology_watch_callback test...\n");

    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    assert(client != NULL);

    service_discovery_t *discovery = service_discovery_create(client);
    assert(discovery != NULL);

    topology_callback_count = 0;
    int result = service_discovery_watch(discovery, test_topology_callback, NULL);
    assert(result== 0);

    service_discovery_destroy(discovery);
    etcd_client_destroy(client);
    printf("topology_watch_callback test PASSED\n");
}

int main(void) {
    printf("=== Service Discovery Tests ===\n\n");

    test_create_returns_non_null();
    test_find_joined_gateway();
    test_topology_watch_callback();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
