#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/bs.h"
#include "lightfs/bs_config.h"
#include "segment.h"
#include "journal.h"
#include "cow_btree.h"

static int g_put_rc;
static blob_location_t g_put_loc;
static int g_get_rc;
static void *g_get_data;
static uint32_t g_get_size;
static int g_delete_rc;

static void test_put_cb(int rc, const blob_location_t *loc, void *arg) {
    g_put_rc = rc;
    if (loc) g_put_loc = *loc;
    *(int *)arg = 1;
}

static void test_get_cb(int rc, const void *data, uint32_t size, void *arg) {
    g_get_rc = rc;
    g_get_data = (void *)data;
    g_get_size = size;
    *(int *)arg = 1;
}

static void test_delete_cb(int rc, void *arg) {
    g_delete_rc = rc;
    *(int *)arg = 1;
}

void test_segment_manager(void) {
    printf("Running segment manager test...\n");

    segment_manager_t *mgr = segment_manager_init(4096);
    assert(mgr != NULL);
    assert(mgr->segment_size == 4096);

    segment_t *seg = segment_alloc(mgr, SEG_TYPE_DATA);
    assert(seg != NULL);
    assert(seg->state == SEG_ACTIVE);

    segment_seal(seg);
    assert(seg->state == SEG_SEALED);

    segment_free(seg);
    assert(seg->state == SEG_FREE);

    segment_manager_destroy(mgr);
    printf("segment manager test PASSED\n");
}

void test_cow_btree(void) {
    printf("Running btree test...\n");

    cow_btree_t *tree = cow_btree_create();
    assert(tree != NULL);

    blob_location_t loc = {.segment_id = 1, .offset = 100, .size = 512};

    int rc = cow_btree_insert(tree, 123, &loc);
    assert(rc == 0);

    blob_location_t lookup_loc;
    rc = cow_btree_lookup(tree, 123, &lookup_loc);
    assert(rc == 0);
    assert(lookup_loc.segment_id == 1);
    assert(lookup_loc.offset == 100);

    rc = cow_btree_delete(tree, 123);
    assert(rc == 0);

    rc = cow_btree_lookup(tree, 123, &lookup_loc);
    assert(rc == -1);

    cow_btree_destroy(tree);
    printf("btree test PASSED\n");
}

void test_journal(void) {
    printf("Running journal test...\n");

    segment_manager_t *mgr = segment_manager_init(4096);
    assert(mgr != NULL);

    journal_t *j = journal_init(mgr);
    assert(j != NULL);

    blob_location_t loc = {.segment_id = 1, .offset = 0, .size = 256};

    int rc = journal_append_put(j, 100, &loc);
    assert(rc == 0);
    assert(j->write_seq == 1);

    rc = journal_append_delete(j, 200);
    assert(rc == 0);
    assert(j->write_seq == 2);

    journal_seal(j);
    assert(j->segment->state == SEG_SEALED);

    journal_destroy(j);
    segment_manager_destroy(mgr);
    printf("journal test PASSED\n");
}

void test_bs_init_destroy(void) {
    printf("Running bs init/destroy test...\n");

    int rc = bs_init(NULL);
    assert(rc == -1);

    bs_config_t cfg = {.segment_size = 4096};
    rc = bs_init(&cfg);
    assert(rc == 0);

    bs_destroy();
    printf("bs init/destroy test PASSED\n");
}

void test_bs_put_get(void) {
    printf("Running bs put/get test...\n");

    bs_config_t cfg = {.segment_size = 4096};
    int rc = bs_init(&cfg);
    assert(rc == 0);

    int done = 0;
    const char *data = "Hello, LightFS!";
    uint32_t size = strlen(data);

    rc = bs_put_blob(1, data, size, test_put_cb, &done);
    assert(rc == 0);
    assert(g_put_rc == 0);
    assert(g_put_loc.size == size);

    done = 0;
    rc = bs_get_blob(&g_put_loc, test_get_cb, &done);
    assert(rc == 0);
    assert(g_get_rc == 0);
    assert(g_get_size == size);

    bs_destroy();
    printf("bs put/get test PASSED\n");
}

void test_bs_delete_stat(void) {
    printf("Running bs delete/stat test...\n");

    bs_config_t cfg = {.segment_size = 4096};
    int rc = bs_init(&cfg);
    assert(rc == 0);

    int done = 0;
    const char *data = "Test data";
    rc = bs_put_blob(999, data, strlen(data), test_put_cb, &done);
    assert(rc == 0);

    blob_state_t state;
    rc = bs_stat_blob(999, &state);
    assert(rc == 0);
    assert(state == BLOB_STATE_ACTIVE);

    done = 0;
    rc = bs_delete_blob(999, test_delete_cb, &done);
    assert(rc == 0);
    assert(g_delete_rc == 0);

    rc = bs_stat_blob(999, &state);
    assert(rc == 0);
    assert(state == BLOB_STATE_FREE);

    bs_destroy();
    printf("bs delete/stat test PASSED\n");
}

int main(void) {
    printf("=== Storage Engine Tests ===\n\n");

    test_segment_manager();
    test_cow_btree();
    test_journal();
    test_bs_init_destroy();
    test_bs_put_get();
    test_bs_delete_stat();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}
