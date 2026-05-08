#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/access_types.h"
#include "access_routes.h"

static void test_parse_put_object_path_style(void) {
    s3_request_t req = {0};
    const char *headers[] = {
        "Content-Length", "1024",
        "Content-Type", "binary/octet-stream",
    };

    int rc = s3_route_parse("PUT", "/mybucket/mykey.txt",
                             headers, 4, &req);
    assert(rc == 0);
    assert(strcmp(req.bucket, "mybucket") == 0);
    assert(strcmp(req.key, "mykey.txt") == 0);
    assert(req.op == S3_OP_PUT_OBJECT);
    assert(req.content_length == 1024);
    printf("  PASS: parse_put_object_path_style\n");
}

static void test_parse_get_object(void) {
    s3_request_t req = {0};

    int rc = s3_route_parse("GET", "/mybucket/data/file.csv",
                             NULL, 0, &req);
    assert(rc == 0);
    assert(strcmp(req.bucket, "mybucket") == 0);
    assert(strcmp(req.key, "data/file.csv") == 0);
    assert(req.op == S3_OP_GET_OBJECT);
    printf("  PASS: parse_get_object\n");
}

static void test_parse_delete_object(void) {
    s3_request_t req = {0};

    int rc = s3_route_parse("DELETE", "/mybucket/old-file.txt",
                             NULL, 0, &req);
    assert(rc == 0);
    assert(strcmp(req.bucket, "mybucket") == 0);
    assert(strcmp(req.key, "old-file.txt") == 0);
    assert(req.op == S3_OP_DELETE_OBJECT);
    printf("  PASS: parse_delete_object\n");
}

static void test_parse_list_objects(void) {
    s3_request_t req = {0};

    int rc = s3_route_parse("GET", "/mybucket?prefix=logs/&max-keys=100",
                             NULL, 0, &req);
    assert(rc == 0);
    assert(strcmp(req.bucket, "mybucket") == 0);
    assert(req.key[0] == '\0');
    assert(req.op == S3_OP_LIST_OBJECTS);
    printf("  PASS: parse_list_objects\n");
}

static void test_parse_unknown_method(void) {
    s3_request_t req = {0};

    int rc = s3_route_parse("PATCH", "/mybucket/key",
                             NULL, 0, &req);
    assert(rc != 0);
    printf("  PASS: parse_unknown_method\n");
}

int main(void) {
    printf("=== test_routes ===\n");
    test_parse_put_object_path_style();
    test_parse_get_object();
    test_parse_delete_object();
    test_parse_list_objects();
    test_parse_unknown_method();
    printf("All route tests passed.\n");
    return 0;
}
