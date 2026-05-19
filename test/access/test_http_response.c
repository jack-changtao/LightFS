#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/uio.h>
#include "lightfs/access/access_types.h"
#include "http_response.h"

static void test_serialize_get_200(void) {
  s3_response_t resp = {
    .http_status = 200,
    .etag = "\"abc123\"",
    .body = (void *)"hello",
    .body_length = 5,
  };

  struct iovec iov[HTTP_IOV_MAX];
  int n = http_response_serialize(&resp, true, iov, HTTP_IOV_MAX);
  assert(n > 0);
  assert(n <= HTTP_IOV_MAX);

  assert(memcmp(iov[0].iov_base, "HTTP/1.1 ", 9) == 0);
  assert(memcmp(iov[1].iov_base, "200 OK\r\n", 9) == 0);

  assert(iov[n-1].iov_base == resp.body);
  assert(iov[n-1].iov_len == 5);

  int found_keep_alive = 0;
  for (int i = 0; i < n; i++) {
    if (iov[i].iov_len >= 24 &&
      memcmp(iov[i].iov_base, "Connection: keep-alive", 22) == 0) {
      found_keep_alive = 1;
      break;
    }
  }
  assert(found_keep_alive);

  printf("  PASS: serialize_get_200\n");
}

static void test_serialize_delete_204(void) {
  s3_response_t resp = {
    .http_status = 204,
  };

  struct iovec iov[HTTP_IOV_MAX];
  int n = http_response_serialize(&resp, true, iov, HTTP_IOV_MAX);
  assert(n > 0);

  assert(memcmp(iov[1].iov_base, "204 No Content\r\n", 17) == 0);
  assert(iov[n-1].iov_len == 2);
  assert(memcmp(iov[n-1].iov_base, "\r\n", 2) == 0);

  printf("  PASS: serialize_delete_204\n");
}

static void test_serialize_close_connection(void) {
  s3_response_t resp = { .http_status = 200 };
  struct iovec iov[HTTP_IOV_MAX];
  int n = http_response_serialize(&resp, false, iov, HTTP_IOV_MAX);

  int found_close = 0;
  for (int i = 0; i < n; i++) {
    if (iov[i].iov_len >= 19 &&
      memcmp(iov[i].iov_base, "Connection: close", 17) == 0) {
      found_close = 1;
      break;
    }
  }
  assert(found_close);
  printf("  PASS: serialize_close_connection\n");
}

static void test_serialize_error_403(void) {
  s3_response_t resp = {
    .http_status = 403,
    .body = (void *)"<Error><Code>AccessDenied</Code></Error>",
    .body_length = 40,
  };

  struct iovec iov[HTTP_IOV_MAX];
  int n = http_response_serialize(&resp, false, iov, HTTP_IOV_MAX);
  assert(n > 0);
  assert(memcmp(iov[1].iov_base, "403 Forbidden\r\n", 16) == 0);
  assert(iov[n-1].iov_base == resp.body);

  printf("  PASS: serialize_error_403\n");
}

int main(void) {
  printf("test_http_response:\n");
  test_serialize_get_200();
  test_serialize_delete_204();
  test_serialize_close_connection();
  test_serialize_error_403();
  printf("  ALL PASS\n");
  return 0;
}
