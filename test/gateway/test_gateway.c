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
    service_discovery_t *sd = service_discovery_create(etcd);
    assert(sd != NULL);

    gateway_config_t cfg = {
        .node_id = 1,
        .dc_id = 0,
        .gateway_port = 8080,
        .default_ec_policy = EC_6_PLUS_3,
        .default_replication_factor = 2,
        .manifest_cache_size = 1000,
    };

    gateway_t *gw = gateway_create(&cfg, sd, etcd);
    assert(gw != NULL);

    gateway_destroy(gw);
    service_discovery_destroy(sd);
    etcd_client_destroy(etcd);
    printf("gateway_create_destroy test PASSED\n");
}

void test_gateway_put_get_small_object(void) {
    printf("Running gateway_put_get_small_object test...\n");

    mock_etcd_init();
    etcd_client_t *etcd = etcd_client_create("127.0.0.1", 2379);
    assert(etcd != NULL);
    service_discovery_t *sd = service_discovery_create(etcd);
    assert(sd != NULL);

    gateway_config_t cfg = {
        .node_id = 1,
        .dc_id = 0,
        .gateway_port = 8080,
        .default_ec_policy = EC_REPLICATION_2X,
        .default_replication_factor = 2,
        .manifest_cache_size = 100,
    };

    gateway_t *gw = gateway_create(&cfg, sd, etcd);
    assert(gw != NULL);

    /* Register nodes for placement */
    struct placement_engine *pe = gateway_get_placement(gw);
    placement_register_node(pe, 1, 0, 0, 1, 1024ULL*1024*1024*1024);
    placement_register_node(pe, 2, 0, 0, 2, 1024ULL*1024*1024*1024);

    char data[512];
    for (int i = 0; i < 512; i++) data[i] = (char)(i & 0xFF);

    gateway_put_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.bucket, "testbucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "hello.txt", sizeof(req.key) - 1);
    req.data = (uint8_t *)data;
    req.size = 512;
    req.ec_override = 0;

    int rc = gateway_put_object(gw, &req);
    assert(rc == 0);

    gateway_get_response_t resp = {0};
    rc = gateway_get_object(gw, "testbucket", "hello.txt", &resp);
    assert(rc == 0);
    assert(resp.size == 512);

    free(resp.data);
    gateway_destroy(gw);
    service_discovery_destroy(sd);
    etcd_client_destroy(etcd);
    printf("gateway_put_get_small_object test PASSED\n");
}

void test_gateway_delete_object(void) {
    printf("Running gateway_delete_object test...\n");

    mock_etcd_init();
    etcd_client_t *etcd = etcd_client_create("127.0.0.1", 2379);
    assert(etcd != NULL);
    service_discovery_t *sd = service_discovery_create(etcd);
    assert(sd != NULL);

    gateway_config_t cfg = {
        .node_id = 1,
        .dc_id = 0,
        .gateway_port = 8080,
        .default_ec_policy = EC_REPLICATION_2X,
        .default_replication_factor = 2,
        .manifest_cache_size = 100,
    };

    gateway_t *gw = gateway_create(&cfg, sd, etcd);
    assert(gw != NULL);

    struct placement_engine *pe = gateway_get_placement(gw);
    placement_register_node(pe, 1, 0, 0, 1, 1024ULL*1024*1024*1024);
    placement_register_node(pe, 2, 0, 0, 2, 1024ULL*1024*1024*1024);

    char data[64];
    memset(data, 0x42, 64);

    gateway_put_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.bucket, "testbucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "todelete.txt", sizeof(req.key) - 1);
    req.data = (uint8_t *)data;
    req.size = 64;
    req.ec_override = 0;

    int rc = gateway_put_object(gw, &req);
    assert(rc == 0);

    rc = gateway_delete_object(gw, "testbucket", "todelete.txt");
    assert(rc == 0);

    gateway_get_response_t resp = {0};
    rc = gateway_get_object(gw, "testbucket", "todelete.txt", &resp);
    assert(rc != 0);

    free(resp.data);
    gateway_destroy(gw);
    service_discovery_destroy(sd);
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
