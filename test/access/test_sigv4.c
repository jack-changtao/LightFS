#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "lightfs/access/sigv4.h"

static void test_validate_missing_header(void) {
  sigv4_result_t result =sigv4_validate(NULL, "GET", "/bucket/key",
                   "s3.amazonaws.com", NULL, NULL, 0);
  assert(result == SIGV4_ERROR_MISSING_HEADER);
  printf("  PASS: validate_missing_header\n");
}

static void test_validate_invalid_auth(void) {
  sigv4_result_t result =sigv4_validate("AWS4-HMAC-SHA256 Credential=INVALID",
                   "GET", "/bucket/key",
                   "s3.amazonaws.com", "20240101T000000Z",
                   NULL, 0);
  assert(result != SIGV4_OK);
  printf("  PASS: validate_invalid_auth\n");
}

static void test_extract_access_key(void) {
  const char *auth = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20240101/us-east-1/s3/aws4_request, "
           "SignedHeaders=host;x-amz-date, Signature=abc123";
  const char *key = sigv4_extract_access_key(auth);
  assert(key != NULL);
  assert(strcmp(key, "AKIAIOSFODNN7EXAMPLE") == 0);
  printf("  PASS: extract_access_key\n");
}

static void test_extract_date(void) {
  const char *date = "20240101T120000Z";
  const char *result = sigv4_extract_date(date);
  assert(result != NULL);
  assert(strcmp(result, "20240101T120000Z") == 0);
  printf("  PASS: extract_date\n");
}

int main(void) {
  printf("=== test_sigv4 ===\n");
  test_validate_missing_header();
  test_validate_invalid_auth();
  test_extract_access_key();
  test_extract_date();
  printf("All SigV4 tests passed.\n");
  return 0;
}
