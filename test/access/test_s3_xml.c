#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/s3_xml.h"

static void test_serialize_list_objects_basic(void) {
    char buffer[4096];
    const char *keys[] = {"file1.txt", "file2.txt"};

    int written =s3_xml_serialize_list_objects(buffer, sizeof(buffer),
                                           "mybucket", "", "", 1000,
                                           keys, 2, 0);
    assert(written > 0);
    assert(strstr(buffer, "mybucket") != NULL);
    assert(strstr(buffer, "file1.txt") != NULL);
    assert(strstr(buffer, "file2.txt") != NULL);
    assert(strstr(buffer, "<IsTruncated>false</IsTruncated>") != NULL);
    printf("  PASS: serialize_list_objects_basic\n");
}

static void test_serialize_list_objects_truncated(void) {
    char buffer[4096];
    const char *keys[] = {"a.txt"};

    int written =s3_xml_serialize_list_objects(buffer, sizeof(buffer),
                                           "mybucket", "", "marker",
                                           1, keys, 1, 1);
    assert(written > 0);
    assert(strstr(buffer, "<IsTruncated>true</IsTruncated>") != NULL);
    assert(strstr(buffer, "<NextMarker>marker</NextMarker>") != NULL);
    printf("  PASS: serialize_list_objects_truncated\n");
}

static void test_serialize_error_response(void) {
    char buffer[1024];
    int written =s3_xml_serialize_error(buffer, sizeof(buffer),
                                    "NoSuchKey", "The specified key does not exist.");
    assert(written > 0);
    assert(strstr(buffer, "<Code>NoSuchKey</Code>") != NULL);
    assert(strstr(buffer, "<Message>") != NULL);
    printf("  PASS: serialize_error_response\n");
}

static void test_serialize_empty_key_list(void) {
    char buffer[4096];
    int written =s3_xml_serialize_list_objects(buffer, sizeof(buffer),
                                           "empty-bucket", "", "", 1000,
                                           NULL, 0, 0);
    assert(written > 0);
    assert(strstr(buffer, "empty-bucket") != NULL);
    printf("  PASS: serialize_empty_key_list\n");
}

int main(void) {
    printf("=== test_s3_xml ===\n");
    test_serialize_list_objects_basic();
    test_serialize_list_objects_truncated();
    test_serialize_error_response();
    test_serialize_empty_key_list();
    printf("All XML tests passed.\n");
    return 0;
}
