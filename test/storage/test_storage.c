#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lightfs/bs.h"
#include "lightfs/bs_config.h"
#include "segment.h"
#include "journal.h"
#include "cow_btree.h"

static int g_put_result;
static blob_location_t g_put_location;
static int g_get_result;
static void *g_get_data;
static uint32_t g_get_size;
static int g_delete_result;

static void test_put_callback(int result, const blob_location_t *location, void *user_data) {
    g_put_result = result;
    if (location) g_put_location = *location;
    *(int *)user_data= 1;
}

static void test_get_callback(int result, const void *data, uint32_t size, void *user_data) {
    g_get_result = result;
    g_get_data = (void *)data;
    g_get_size = size;
    *(int *)user_data= 1;
}

static void test_delete_callback(int result, void *user_data) {
    g_delete_result = result;
    *(int *)user_data= 1;
}

void test_segment_manager(void) {
    printf("Running segment manager test...\n");

    segment_manager_t *manager = segment_manager_initialize(4096);
    assert(manager != NULL);
    assert(manager->segment_size == 4096);

    segment_t *segment = segment_allocate(manager, SEGMENT_TYPE_DATA);
    assert(segment != NULL);
    assert(segment->state == SEGMENT_ACTIVE);

    segment_seal(segment);
    assert(segment->state == SEGMENT_SEALED);

    segment_free(segment);
    assert(segment->state == SEGMENT_FREE);

    segment_manager_destroy(manager);
    printf("segment manager test PASSED\n");
}

void test_cow_btree(void) {
    printf("Running btree test...\n");

    cow_btree_t *tree = cow_btree_create();
    assert(tree != NULL);

    blob_location_t location = {.segment_id = 1, .offset = 100, .size = 512};

    int result = cow_btree_insert(tree, 123, &location);
    assert(result== 0);

    blob_location_t lookup_location;
    result =cow_btree_lookup(tree, 123, &lookup_loc);
    assert(result== 0);
    assert(lookup_location.segment_id == 1);
    assert(lookup_location.offset == 100);

    result =cow_btree_delete(tree, 123);
    assert(result== 0);

    result =cow_btree_lookup(tree, 123, &lookup_loc);
    assert(result== -1);

    cow_btree_destroy(tree);
    printf("btree test PASSED\n");
}

void test_journal(void) {
    printf("Running journal test...\n");

    segment_manager_t *manager = segment_manager_initialize(4096);
    assert(manager != NULL);

    journal_t *journal = journal_init(manager);
    assert(journal != NULL);

    blob_location_t location = {.segment_id = 1, .offset = 0, .size = 256};

    int result = journal_append_put(journal, 100, &location);
    assert(result== 0);
    assert(journal->write_sequence == 1);

    result =journal_append_delete(journal, 200);
    assert(result== 0);
    assert(journal->write_sequence == 2);

    journal_seal(journal);
    assert(journal->segment->state == SEGMENT_SEALED);

    journal_destroy(journal);
    segment_manager_destroy(manager);
    printf("journal test PASSED\n");
}

void test_bs_init_destroy(void) {
    printf("Running bs init/destroy test...\n");

    int result = bs_init(NULL);
    assert(result== -1);

    bs_config_t config = {.segment_size = 4096};
    result =bs_init(&config);
    assert(result== 0);

    bs_destroy();
    printf("bs init/destroy test PASSED\n");
}

void test_bs_put_get(void) {
    printf("Running bs put/get test...\n");

    bs_config_t config = {.segment_size = 4096};
    int result = bs_init(&config);
    assert(result== 0);

    int done = 0;
    const char *data = "Hello, LightFS!";
    uint32_t size = strlen(data);

    result =bs_put_blob(1, data, size, test_put_callback, &done);
    assert(result== 0);
    assert(g_put_result == 0);
    assert(g_put_location.size == size);

    done = 0;
    result =bs_get_blob(&g_put_location, test_get_callback, &done);
    assert(result== 0);
    assert(g_get_result == 0);
    assert(g_get_size == size);

    bs_destroy();
    printf("bs put/get test PASSED\n");
}

void test_bs_delete_stat(void) {
    printf("Running bs delete/stat test...\n");

    bs_config_t config = {.segment_size = 4096};
    int result = bs_init(&config);
    assert(result== 0);

    int done = 0;
    const char *data = "Test data";
    result =bs_put_blob(999, data, strlen(data), test_put_callback, &done);
    assert(result== 0);

    blob_state_t state;
    result =bs_stat_blob(999, &state);
    assert(result== 0);
    assert(state == BLOB_STATE_ACTIVE);

    done = 0;
    result =bs_delete_blob(999, test_delete_callback, &done);
    assert(result== 0);
    assert(g_delete_result == 0);

    result =bs_stat_blob(999, &state);
    assert(result== 0);
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
