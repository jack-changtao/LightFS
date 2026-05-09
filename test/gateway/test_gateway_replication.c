#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/gateway/gateway_replication.h"

void test_add_peer(void) {
    printf("Running add_peer test...\n");

    replication_engine_t *re = replication_engine_create();
    assert(re != NULL);

    replication_peer_t peer = {
        .peer_dc_id = 2,
        .peer_gateway_host = "10.0.2.1",
        .peer_gateway_port = 8080,
    };
    int rc = replication_add_peer(re, &peer);
    assert(rc == 0);

    replication_engine_destroy(re);
    printf("add_peer test PASSED\n");
}

void test_enqueue_replication(void) {
    printf("Running enqueue_replication test...\n");

    replication_engine_t *re = replication_engine_create();
    assert(re != NULL);

    replication_peer_t peer = {.peer_dc_id = 2, .peer_gateway_host = "10.0.2.1",
                                .peer_gateway_port = 8080};
    replication_add_peer(re, &peer);

    object_manifest_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.bucket, "geo-bucket", sizeof(m.bucket) - 1);
    strncpy(m.key, "obj1", sizeof(m.key) - 1);
    m.size = 2048;
    m.write_seq = 100;
    m.dc_id = 1;

    int rc = replication_enqueue(re, &m, NULL, m.size);
    assert(rc == 0);
    assert(replication_pending_count(re) == 1);

    replication_engine_destroy(re);
    printf("enqueue_replication test PASSED\n");
}

void test_lww_conflict_resolution(void) {
    printf("Running lww_conflict_resolution test...\n");

    int rc = replication_resolve_conflict(200, 1, 100, 2);
    assert(rc == 1);

    rc = replication_resolve_conflict(50, 1, 100, 2);
    assert(rc == 0);

    rc = replication_resolve_conflict(100, 2, 100, 1);
    assert(rc == 1);

    rc = replication_resolve_conflict(100, 1, 100, 2);
    assert(rc == 0);

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
