#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/access_types.h"
#include "access_handlers.h"
#include "../mocks/mock_gateway.h"

static void test_put_object_success(void) {
    mock_gateway_reset();

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "test.txt", sizeof(req.key) - 1);
    req.op = S3_OP_PUT_OBJECT;
    req.content_length = 13;

    const char *body = "hello, world!";
    s3_response_t resp = {0};

    int rc = s3_handler_put(&req, body, 13, &resp);
    assert(rc == 0);
    assert(resp.http_status == 200);
    printf("  PASS: put_object_success\n");
}

static void test_get_object_success(void) {
    mock_gateway_reset();

    const char *mock_data = "stored object data";
    mock_gateway_set_response(0, mock_data, 18);

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "test.txt", sizeof(req.key) - 1);
    req.op = S3_OP_GET_OBJECT;

    s3_response_t resp = {0};
    int rc = s3_handler_get(&req, &resp);
    assert(rc == 0);
    assert(resp.http_status == 200);
    assert(resp.body != NULL);
    printf("  PASS: get_object_success\n");
}

static void test_delete_object_success(void) {
    mock_gateway_reset();

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "old.txt", sizeof(req.key) - 1);
    req.op = S3_OP_DELETE_OBJECT;

    s3_response_t resp = {0};
    int rc = s3_handler_delete(&req, &resp);
    assert(rc == 0);
    assert(resp.http_status == 204);
    printf("  PASS: delete_object_success\n");
}

static void test_list_objects_success(void) {
    mock_gateway_reset();

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    req.op = S3_OP_LIST_OBJECTS;

    s3_response_t resp = {0};
    int rc = s3_handler_list(&req, &resp);
    assert(rc == 0);
    assert(resp.http_status == 200);
    assert(resp.body != NULL);
    printf("  PASS: list_objects_success\n");
}

int main(void) {
    printf("=== test_handlers ===\n");
    test_put_object_success();
    test_get_object_success();
    test_delete_object_success();
    test_list_objects_success();
    printf("All handler tests passed.\n");
    return 0;
}
