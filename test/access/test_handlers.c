#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/access_types.h"
#include "access_handlers.h"
#include "../mocks/mock_gateway.h"

static void test_put_object_success(void) {
    mock_gateway_reset();

    s3_request_t request = {0};
    strncpy(request.bucket, "mybucket", sizeof(request.bucket) - 1);
    strncpy(request.key, "test.txt", sizeof(request.key) - 1);
    request.operation =S3_OPERATION_PUT_OBJECT;
    request.content_length = 13;

    const char *body = "hello, world!";
    s3_response_t response = {0};

    int result = s3_handler_put(&request, body, 13, &response);
    assert(result== 0);
    assert(response.http_status == 200);
    printf("  PASS: put_object_success\n");
}

static void test_get_object_success(void) {
    mock_gateway_reset();

    const char *mock_data = "stored object data";
    mock_gateway_set_response(0, mock_data, 18);

    s3_request_t request = {0};
    strncpy(request.bucket, "mybucket", sizeof(request.bucket) - 1);
    strncpy(request.key, "test.txt", sizeof(request.key) - 1);
    request.operation =S3_OPERATION_GET_OBJECT;

    s3_response_t response = {0};
    int result = s3_handler_get(&request, &response);
    assert(result== 0);
    assert(response.http_status == 200);
    assert(response.body != NULL);
    printf("  PASS: get_object_success\n");
}

static void test_delete_object_success(void) {
    mock_gateway_reset();

    s3_request_t request = {0};
    strncpy(request.bucket, "mybucket", sizeof(request.bucket) - 1);
    strncpy(request.key, "old.txt", sizeof(request.key) - 1);
    request.operation =S3_OPERATION_DELETE_OBJECT;

    s3_response_t response = {0};
    int result = s3_handler_delete(&request, &response);
    assert(result== 0);
    assert(response.http_status == 204);
    printf("  PASS: delete_object_success\n");
}

static void test_list_objects_success(void) {
    mock_gateway_reset();

    s3_request_t request = {0};
    strncpy(request.bucket, "mybucket", sizeof(request.bucket) - 1);
    request.operation =S3_OPERATION_LIST_OBJECTS;

    s3_response_t response = {0};
    int result = s3_handler_list(&request, &response);
    assert(result== 0);
    assert(response.http_status == 200);
    assert(response.body != NULL);
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
