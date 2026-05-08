#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/s3_xml.h"

static void test_serialize_list_objects_basic(void) {
    char buf[4096];
    const char *keys[] = {"file1.txt", "file2.txt"};

    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "mybucket", "", "", 1000,
                                           keys, 2, 0);
    assert(n > 0);
    assert(strstr(buf, "mybucket") != NULL);
    assert(strstr(buf, "file1.txt") != NULL);
    assert(strstr(buf, "file2.txt") != NULL);
    assert(strstr(buf, "<IsTruncated>false</IsTruncated>") != NULL);
    printf("  PASS: serialize_list_objects_basic\n");
}

static void test_serialize_list_objects_truncated(void) {
    char buf[4096];
    const char *keys[] = {"a.txt"};

    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "mybucket", "", "marker",
                                           1, keys, 1, 1);
    assert(n > 0);
    assert(strstr(buf, "<IsTruncated>true</IsTruncated>") != NULL);
    assert(strstr(buf, "<NextMarker>marker</NextMarker>") != NULL);
    printf("  PASS: serialize_list_objects_truncated\n");
}

static void test_serialize_error_response(void) {
    char buf[1024];
    int n = s3_xml_serialize_error(buf, sizeof(buf),
                                    "NoSuchKey", "The specified key does not exist.");
    assert(n > 0);
    assert(strstr(buf, "<Code>NoSuchKey</Code>") != NULL);
    assert(strstr(buf, "<Message>") != NULL);
    printf("  PASS: serialize_error_response\n");
}

static void test_serialize_empty_key_list(void) {
    char buf[4096];
    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "empty-bucket", "", "", 1000,
                                           NULL, 0, 0);
    assert(n > 0);
    assert(strstr(buf, "empty-bucket") != NULL);
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
