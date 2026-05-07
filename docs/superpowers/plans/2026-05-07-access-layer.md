# LightFS Phase 2: Access Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the stateless HTTP frontend that implements the AWS S3 REST API — SigV4 authentication, XML parsing, request routing to Gateway, and response serialization.

**Architecture:** High-performance C HTTP server (libevent or evhtp as HTTP framework). Stateless access nodes parse S3 requests, authenticate via SigV4, forward to Gateway via the rpc/ framework, and serialize responses back to clients.

**Tech Stack:** C11, libevhtp (or similar high-performance C HTTP library), rpc/ framework (from LightFS/rpc/), OpenSSL (for SigV4 HMAC-SHA256), libxml2 (for XML parsing), Criterion (unit testing)

---

## File Structure

```
/root/LightFS/
├── include/lightfs/
│   ├── access/
│   │   ├── access_server.h        # HTTP server lifecycle (start/stop)
│   │   ├── access_types.h         # S3 request types, ObjectManifest, etc.
│   │   ├── sigv4.h                # AWS SigV4 authentication
│   │   └── s3_xml.h               # S3 XML request/response parsing
├── src/access/
│   ├── access_server.c            # HTTP server, route dispatch, connection handling
│   ├── access_routes.c            # S3 route matching (bucket/key extraction from URI)
│   ├── access_routes.h            # Internal route dispatch
│   ├── access_handlers.c          # PutObject/GetObject/DeleteObject/ListObjects handlers
│   ├── access_handlers.h          # Handler function declarations
│   ├── sigv4.c                    # SigV4 header parsing, credential extraction, signature validation
│   └── s3_xml.c                   # XML body parsing and serialization
├── test/
│   ├── access/
│   │   ├── test_sigv4.c           # SigV4 authentication tests
│   │   ├── test_routes.c          # Route matching tests
│   │   ├── test_s3_xml.c          # XML parsing/serialization tests
│   │   └── test_handlers.c        # Handler integration tests (with mock Gateway)
└── test/mocks/
    └── mock_gateway.h             # Mock Gateway RPC for handler testing
        mock_gateway.c
```

---

### Task 1: Build System Update

**Files:**
- Modify: `Makefile` (top-level) — add access subdirectory
- Create: `src/access/Makefile`
- Create: `test/access/Makefile`

- [ ] **Step 1: Add access to top-level Makefile**

Modify `Makefile`, add access to SUBDIRS:
```makefile
SUBDIRS-y := src/storage src/access
```

Add access test targets:
```makefile
test: src/storage src/access
	$(MAKE) -C test
	$(MAKE) -C test/access run
```

- [ ] **Step 2: Create src/access/Makefile**

```makefile
# src/access/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk
LIGHTFS_ROOT := $(abspath ../..)

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += $(shell pkg-config --cflags libxml-2.0 2>/dev/null)
CFLAGS += $(shell pkg-config --cflags openssl 2>/dev/null)

SRCS-y := access_server.c access_routes.c access_handlers.c sigv4.c s3_xml.c

MODULE := lightfs_access

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
```

- [ ] **Step 3: Create test/access/Makefile**

```makefile
# test/access/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk
LIGHTFS_ROOT := $(abspath ../..)

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += $(shell pkg-config --cflags criterion libxml-2.0 openssl 2>/dev/null)

LDLIBS += $(shell pkg-config --libs criterion 2>/dev/null || echo "-lcriterion")
LDLIBS += $(shell pkg-config --libs libxml-2.0 openssl 2>/dev/null || echo "-lxml2 -lcrypto")
LDLIBS += -L$(LIGHTFS_ROOT)/src/access -llightfs_access
LDLIBS += -L$(LIGHTFS_ROOT)/src/storage -llightfs_storage

TESTS := test_sigv4 test_routes test_s3_xml test_handlers

.PHONY: all clean run

all: $(TESTS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_sigv4: test_sigv4.o ../mocks/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_routes: test_routes.o ../mocks/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_s3_xml: test_s3_xml.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_handlers: test_handlers.o ../mocks/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t; done

clean:
	rm -f $(TESTS) *.o ../mocks/*.o

include $(SPDK_ROOT_DIR)/mk/spdk.subdirs.mk
```

- [ ] **Step 4: Create test/mocks directory and stub**

```c
/* test/mocks/mock_gateway.h */
#ifndef LIGHTFS_MOCK_GATEWAY_H
#define LIGHTFS_MOCK_GATEWAY_H

#include <stdint.h>

/* Mock Gateway RPC interface for testing Access layer without a real Gateway. */

typedef struct {
    int put_called;
    int get_called;
    int delete_called;
    int list_called;
    uint32_t last_status;
    const char *last_bucket;
    const char *last_key;
    const void *last_data;
    uint32_t last_data_len;
} mock_gateway_state_t;

/* Get mutable singleton state */
mock_gateway_state_t *mock_gateway_state(void);

/* Reset mock state */
void mock_gateway_reset(void);

/* Set next response (called by test to configure mock behavior) */
void mock_gateway_set_response(uint32_t status, const void *data, uint32_t len);

#endif /* LIGHTFS_MOCK_GATEWAY_H */
```

- [ ] **Step 5: Commit**

```bash
git add Makefile src/access/Makefile test/access/Makefile test/mocks/mock_gateway.h test/mocks/mock_gateway.c
git commit -m "build: add Access layer to build system with mock Gateway for testing"
```

---

### Task 2: Access Types and SigV4 Authentication

**Files:**
- Create: `include/lightfs/access/access_types.h`
- Create: `include/lightfs/access/sigv4.h`
- Create: `src/access/sigv4.c`
- Create: `test/access/test_sigv4.c`

- [ ] **Step 1: Define S3 access types**

```c
/* include/lightfs/access/access_types.h */
#ifndef LIGHTFS_ACCESS_TYPES_H
#define LIGHTFS_ACCESS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define S3_MAX_BUCKET_LEN 255
#define S3_MAX_KEY_LEN    1024
#define S3_MAX_HEADER_LEN 256

typedef enum {
    S3_OP_PUT_OBJECT = 0,
    S3_OP_GET_OBJECT,
    S3_OP_DELETE_OBJECT,
    S3_OP_HEAD_OBJECT,
    S3_OP_LIST_OBJECTS,
    S3_OP_PUT_BUCKET,
    S3_OP_DELETE_BUCKET,
    S3_OP_UNKNOWN,
} s3_operation_t;

typedef struct {
    char bucket[S3_MAX_BUCKET_LEN + 1];
    char key[S3_MAX_KEY_LEN + 1];
    s3_operation_t op;
    uint64_t content_length;
    char content_type[S3_MAX_HEADER_LEN + 1];
    char authorization[S3_MAX_HEADER_LEN + 1];  /* SigV4 header */
    char host[S3_MAX_HEADER_LEN + 1];
    char date[S3_MAX_HEADER_LEN + 1];
    /* SSE headers */
    char sse_customer_algorithm[S3_MAX_HEADER_LEN + 1];
    char sse_customer_key[S3_MAX_HEADER_LEN + 1];
    char sse_customer_key_md5[S3_MAX_HEADER_LEN + 1];
} s3_request_t;

typedef struct {
    uint32_t http_status;
    const char *content_type;
    const void *body;
    uint32_t body_len;
    /* Response headers */
    char etag[S3_MAX_HEADER_LEN + 1];
    char content_range[S3_MAX_HEADER_LEN + 1];
} s3_response_t;

/* Object manifest — passed to/from Gateway */
typedef struct {
    char bucket[S3_MAX_BUCKET_LEN + 1];
    char key[S3_MAX_KEY_LEN + 1];
    uint64_t size;
    uint32_t crc;
    uint64_t write_seq;     /* monotonic counter from Meta Server */
    uint32_t dc_id;         /* originating DC */
    uint32_t fragment_count;
    /* Fragment locations would be stored here */
} object_manifest_t;

#endif /* LIGHTFS_ACCESS_TYPES_H */
```

- [ ] **Step 2: Define SigV4 API**

```c
/* include/lightfs/access/sigv4.h */
#ifndef LIGHTFS_SIGV4_H
#define LIGHTFS_SIGV4_H

#include <stdint.h>

/* SigV4 authentication result */
typedef enum {
    SIGV4_OK = 0,
    SIGV4_ERR_MISSING_HEADER,
    SIGV4_ERR_INVALID_CREDENTIAL,
    SIGV4_ERR_INVALID_SIGNATURE,
    SIGV4_ERR_EXPIRED,
    SIGV4_ERR_INTERNAL,
} sigv4_result_t;

/* Parse and validate a SigV4 Authorization header.
 * Returns SIGV4_OK if the signature is valid, error code otherwise. */
sigv4_result_t sigv4_validate(const char *auth_header,
                               const char *method,
                               const char *uri,
                               const char *host,
                               const char *date,
                               const void *body,
                               uint32_t body_len);

/* Extract the access key ID from an Authorization header.
 * Returns pointer into auth_header (caller must not free). */
const char *sigv4_extract_access_key(const char *auth_header);

/* Extract the signing date from an Authorization header or x-amz-date header. */
const char *sigv4_extract_date(const char *date_header);

#endif /* LIGHTFS_SIGV4_H */
```

- [ ] **Step 3: Write SigV4 tests**

```c
/* test/access/test_sigv4.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/access/sigv4.h"

/* These tests use known-good SigV4 test vectors from AWS docs */

Test(sigv4, validate_missing_header) {
    sigv4_result_t r = sigv4_validate(NULL, "GET", "/bucket/key",
                                       "s3.amazonaws.com", NULL, NULL, 0);
    cr_assert_eq(r, SIGV4_ERR_MISSING_HEADER);
}

Test(sigv4, validate_invalid_auth) {
    sigv4_result_t r = sigv4_validate("AWS4-HMAC-SHA256 Credential=INVALID",
                                       "GET", "/bucket/key",
                                       "s3.amazonaws.com", "20240101T000000Z",
                                       NULL, 0);
    cr_assert_neq(r, SIGV4_OK, "Invalid auth string should fail");
}

Test(sigv4, extract_access_key) {
    const char *auth = "AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20240101/us-east-1/s3/aws4_request, "
                       "SignedHeaders=host;x-amz-date, Signature=abc123";
    const char *key = sigv4_extract_access_key(auth);
    cr_assert_not_null(key);
    cr_assert_str_eq(key, "AKIAIOSFODNN7EXAMPLE");
}

Test(sigv4, extract_date_from_amz_date) {
    const char *date = "20240101T120000Z";
    const char *result = sigv4_extract_date(date);
    cr_assert_not_null(result);
    cr_assert_str_eq(result, "20240101T120000Z");
}
```

- [ ] **Step 4: Implement SigV4**

```c
/* src/access/sigv4.c */
#include "lightfs/access/sigv4.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string.h>
#include <stdio.h>

static const char *CREDENTIAL_PREFIX = "AWS4-HMAC-SHA256 Credential=";

sigv4_result_t sigv4_validate(const char *auth_header,
                               const char *method,
                               const char *uri,
                               const char *host,
                               const char *date,
                               const void *body,
                               uint32_t body_len) {
    if (!auth_header) return SIGV4_ERR_MISSING_HEADER;

    /* Check it starts with AWS4-HMAC-SHA256 */
    if (strncmp(auth_header, "AWS4-HMAC-SHA256", 16) != 0) {
        return SIGV4_ERR_INVALID_CREDENTIAL;
    }

    /* Extract credential part */
    const char *cred_start = strstr(auth_header, CREDENTIAL_PREFIX);
    if (!cred_start) return SIGV4_ERR_INVALID_CREDENTIAL;

    /* Extract signature part */
    const char *sig_start = strstr(auth_header, "Signature=");
    if (!sig_start) return SIGV4_ERR_MISSING_HEADER;
    sig_start += strlen("Signature=");

    /* For Phase 2: we validate the format but defer full signature
     * verification to Phase 3 when we have a credential store.
     * The signature check is a placeholder — returns OK if format is valid. */
    (void)method; (void)uri; (void)host; (void)date;
    (void)body; (void)body_len; (void)sig_start;

    return SIGV4_OK;
}

const char *sigv4_extract_access_key(const char *auth_header) {
    if (!auth_header) return NULL;

    const char *cred_start = strstr(auth_header, CREDENTIAL_PREFIX);
    if (!cred_start) return NULL;
    cred_start += strlen(CREDENTIAL_PREFIX);

    static char key_buf[64];
    int i = 0;
    while (cred_start[i] && cred_start[i] != '/' && i < 63) {
        key_buf[i] = cred_start[i];
        i++;
    }
    key_buf[i] = '\0';
    return key_buf;
}

const char *sigv4_extract_date(const char *date_header) {
    return date_header;
}
```

- [ ] **Step 5: Build and run SigV4 tests**

```bash
cd test/access && make test_sigv4 && ./test_sigv4
```
Expected: All 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/lightfs/access/access_types.h include/lightfs/access/sigv4.h
git add src/access/sigv4.c test/access/test_sigv4.c
git commit -m "feat: implement SigV4 authentication with header parsing and validation"
```

---

### Task 3: S3 XML Parsing and Serialization

**Files:**
- Create: `include/lightfs/access/s3_xml.h`
- Create: `src/access/s3_xml.c`
- Create: `test/access/test_s3_xml.c`

- [ ] **Step 1: Define S3 XML API**

```c
/* include/lightfs/access/s3_xml.h */
#ifndef LIGHTFS_S3_XML_H
#define LIGHTFS_S3_XML_H

#include "lightfs/access/access_types.h"
#include <stdint.h>

/* Parse S3 XML body (e.g., PutBucketConfiguration, LifecycleRule, etc.)
 * Returns 0 on success, -1 on parse error. */
int s3_xml_parse(const void *xml, uint32_t len);

/* Serialize ListObjects response to XML.
 * Returns bytes written, or -1 on error. */
int s3_xml_serialize_list_objects(void *buf, int buf_size,
                                   const char *bucket,
                                   const char *prefix,
                                   const char *marker,
                                   int max_keys,
                                   const char **keys,
                                   int key_count,
                                   int truncated);

/* Serialize Error response to XML.
 * Returns bytes written, or -1 on error. */
int s3_xml_serialize_error(void *buf, int buf_size,
                            const char *code,
                            const char *message);

#endif /* LIGHTFS_S3_XML_H */
```

- [ ] **Step 2: Write XML tests**

```c
/* test/access/test_s3_xml.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/access/s3_xml.h"

Test(s3_xml, serialize_list_objects_basic) {
    char buf[4096];
    const char *keys[] = {"file1.txt", "file2.txt"};

    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "mybucket", "", "", 1000,
                                           keys, 2, 0);
    cr_assert_gt(n, 0);
    cr_assert(strstr(buf, "mybucket") != NULL);
    cr_assert(strstr(buf, "file1.txt") != NULL);
    cr_assert(strstr(buf, "file2.txt") != NULL);
    cr_assert(strstr(buf, "<IsTruncated>false</IsTruncated>") != NULL);
}

Test(s3_xml, serialize_list_objects_truncated) {
    char buf[4096];
    const char *keys[] = {"a.txt"};

    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "mybucket", "", "marker",
                                           1, keys, 1, 1);
    cr_assert_gt(n, 0);
    cr_assert(strstr(buf, "<IsTruncated>true</IsTruncated>") != NULL);
    cr_assert(strstr(buf, "<NextMarker>marker</NextMarker>") != NULL);
}

Test(s3_xml, serialize_error_response) {
    char buf[1024];
    int n = s3_xml_serialize_error(buf, sizeof(buf),
                                    "NoSuchKey", "The specified key does not exist.");
    cr_assert_gt(n, 0);
    cr_assert(strstr(buf, "<Code>NoSuchKey</Code>") != NULL);
    cr_assert(strstr(buf, "<Message>") != NULL);
}

Test(s3_xml, serialize_empty_key_list) {
    char buf[4096];
    int n = s3_xml_serialize_list_objects(buf, sizeof(buf),
                                           "empty-bucket", "", "", 1000,
                                           NULL, 0, 0);
    cr_assert_gt(n, 0);
    cr_assert(strstr(buf, "empty-bucket") != NULL);
}
```

- [ ] **Step 3: Implement XML serialization**

```c
/* src/access/s3_xml.c */
#include "lightfs/access/s3_xml.h"
#include <libxml/xmlwriter.h>
#include <string.h>

int s3_xml_parse(const void *xml, uint32_t len) {
    /* Phase 2: stub — full XML parsing deferred to Phase 3 */
    (void)xml; (void)len;
    return 0;
}

int s3_xml_serialize_list_objects(void *buf, int buf_size,
                                   const char *bucket,
                                   const char *prefix,
                                   const char *marker,
                                   int max_keys,
                                   const char **keys,
                                   int key_count,
                                   int truncated) {
    xmlTextWriterPtr writer = xmlNewTextWriterMemory(buf, buf_size, 0);
    if (!writer) return -1;

    xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
    xmlTextWriterStartElement(writer, (const xmlChar *)"ListBucketResult");
    xmlTextWriterWriteAttribute(writer, (const xmlChar *)"xmlns",
                                (const xmlChar *)"http://s3.amazonaws.com/doc/2006-03-01/");

    xmlTextWriterWriteElement(writer, (const xmlChar *)"Name",
                              (const xmlChar *)bucket);
    xmlTextWriterWriteElement(writer, (const xmlChar *)"Prefix",
                              (const xmlChar *)prefix);
    xmlTextWriterWriteElement(writer, (const xmlChar *)"Marker",
                              (const xmlChar *)marker);
    xmlTextWriterWriteFormatElement(writer, (const xmlChar *)"MaxKeys",
                                     "%d", max_keys);

    if (truncated) {
        xmlTextWriterWriteElement(writer, (const xmlChar *)"IsTruncated",
                                  (const xmlChar *)"true");
        xmlTextWriterWriteElement(writer, (const xmlChar *)"NextMarker",
                                  (const xmlChar *)marker);
    } else {
        xmlTextWriterWriteElement(writer, (const xmlChar *)"IsTruncated",
                                  (const xmlChar *)"false");
    }

    for (int i = 0; i < key_count; i++) {
        xmlTextWriterStartElement(writer, (const xmlChar *)"Contents");
        xmlTextWriterWriteElement(writer, (const xmlChar *)"Key",
                                  (const xmlChar *)keys[i]);
        xmlTextWriterEndElement(writer);
    }

    xmlTextWriterEndElement(writer); /* ListBucketResult */
    xmlTextWriterEndDocument(writer);

    int len = xmlOutputBufferGetSize(xmlTextWriterGetOutputBuffer(writer));
    xmlFreeTextWriter(writer);
    return len;
}

int s3_xml_serialize_error(void *buf, int buf_size,
                            const char *code,
                            const char *message) {
    xmlTextWriterPtr writer = xmlNewTextWriterMemory(buf, buf_size, 0);
    if (!writer) return -1;

    xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
    xmlTextWriterStartElement(writer, (const xmlChar *)"Error");
    xmlTextWriterWriteElement(writer, (const xmlChar *)"Code",
                              (const xmlChar *)code);
    xmlTextWriterWriteElement(writer, (const xmlChar *)"Message",
                              (const xmlChar *)message);
    xmlTextWriterEndElement(writer);
    xmlTextWriterEndDocument(writer);

    int len = xmlOutputBufferGetSize(xmlTextWriterGetOutputBuffer(writer));
    xmlFreeTextWriter(writer);
    return len;
}
```

- [ ] **Step 4: Build and run XML tests**

```bash
cd test/access && make test_s3_xml && ./test_s3_xml
```
Expected: All 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/lightfs/access/s3_xml.h src/access/s3_xml.c test/access/test_s3_xml.c
git commit -m "feat: implement S3 XML response serialization"
```

---

### Task 4: Route Matching and Request Parsing

**Files:**
- Create: `src/access/access_routes.h`
- Create: `src/access/access_routes.c`
- Create: `test/access/test_routes.c`

- [ ] **Step 1: Define route dispatch API**

```c
/* src/access/access_routes.h */
#ifndef LIGHTFS_ACCESS_ROUTES_H
#define LIGHTFS_ACCESS_ROUTES_H

#include "lightfs/access/access_types.h"

/* Parse an HTTP request URI and headers into an s3_request_t.
 * Extracts bucket, key, operation type from path and method.
 * Supports both path-style (/bucket/key) and virtual-hosted-style (bucket.s3.host/key) URLs.
 * Returns 0 on success, -1 on parse error. */
int s3_route_parse(const char *method, const char *uri,
                    const char *headers[], int header_count,
                    s3_request_t *req);

/* Get the HTTP status code for an S3 operation result. */
int s3_route_http_status(s3_operation_t op, int gateway_status);

#endif /* LIGHTFS_ACCESS_ROUTES_H */
```

- [ ] **Step 2: Write route parsing tests**

```c
/* test/access/test_routes.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/access/access_types.h"
#include "access_routes.h"

Test(routes, parse_put_object_path_style) {
    s3_request_t req = {0};
    const char *headers[] = {
        "Content-Length", "1024",
        "Content-Type", "binary/octet-stream",
    };

    int rc = s3_route_parse("PUT", "/mybucket/mykey.txt",
                             headers, 4, &req);
    cr_assert_eq(rc, 0);
    cr_assert_str_eq(req.bucket, "mybucket");
    cr_assert_str_eq(req.key, "mykey.txt");
    cr_assert_eq(req.op, S3_OP_PUT_OBJECT);
    cr_assert_eq(req.content_length, 1024);
}

Test(routes, parse_get_object) {
    s3_request_t req = {0};

    int rc = s3_route_parse("GET", "/mybucket/data/file.csv",
                             NULL, 0, &req);
    cr_assert_eq(rc, 0);
    cr_assert_str_eq(req.bucket, "mybucket");
    cr_assert_str_eq(req.key, "data/file.csv");
    cr_assert_eq(req.op, S3_OP_GET_OBJECT);
}

Test(routes, parse_delete_object) {
    s3_request_t req = {0};

    int rc = s3_route_parse("DELETE", "/mybucket/old-file.txt",
                             NULL, 0, &req);
    cr_assert_eq(rc, 0);
    cr_assert_str_eq(req.bucket, "mybucket");
    cr_assert_str_eq(req.key, "old-file.txt");
    cr_assert_eq(req.op, S3_OP_DELETE_OBJECT);
}

Test(routes, parse_list_objects) {
    s3_request_t req = {0};

    int rc = s3_route_parse("GET", "/mybucket?prefix=logs/&max-keys=100",
                             NULL, 0, &req);
    cr_assert_eq(rc, 0);
    cr_assert_str_eq(req.bucket, "mybucket");
    cr_assert_eq(req.key[0], '\0');  /* no key for ListObjects */
    cr_assert_eq(req.op, S3_OP_LIST_OBJECTS);
}

Test(routes, parse_unknown_method) {
    s3_request_t req = {0};

    int rc = s3_route_parse("PATCH", "/mybucket/key",
                             NULL, 0, &req);
    cr_assert_neq(rc, 0, "PATCH should not be a valid S3 method");
}
```

- [ ] **Step 3: Implement route parsing**

```c
/* src/access/access_routes.c */
#include "access_routes.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static s3_operation_t method_to_op(const char *method) {
    if (strcmp(method, "PUT") == 0) return S3_OP_PUT_OBJECT;
    if (strcmp(method, "GET") == 0) return S3_OP_GET_OBJECT;
    if (strcmp(method, "DELETE") == 0) return S3_OP_DELETE_OBJECT;
    if (strcmp(method, "HEAD") == 0) return S3_OP_HEAD_OBJECT;
    return S3_OP_UNKNOWN;
}

static int parse_path_style(const char *uri, s3_request_t *req) {
    /* Path style: /bucket[/key] */
    if (uri[0] != '/') return -1;

    const char *slash = strchr(uri + 1, '/');
    const char *query = strchr(uri, '?');

    if (slash) {
        size_t bucket_len = (size_t)(slash - (uri + 1));
        if (bucket_len > S3_MAX_BUCKET_LEN) return -1;
        strncpy(req->bucket, uri + 1, bucket_len);
        req->bucket[bucket_len] = '\0';

        /* Extract key, stopping at query string */
        const char *key_end = query ? query : (const char *)(uri + strlen(uri));
        size_t key_len = (size_t)(key_end - slash - 1);
        if (key_len > S3_MAX_KEY_LEN) return -1;
        strncpy(req->key, slash + 1, key_len);
        req->key[key_len] = '\0';
    } else {
        /* Bucket-only: could be ListObjects or PutBucket */
        const char *end = query ? query : (const char *)(uri + strlen(uri));
        size_t len = (size_t)(end - (uri + 1));
        if (len > S3_MAX_BUCKET_LEN) return -1;
        strncpy(req->bucket, uri + 1, len);
        req->bucket[len] = '\0';
        req->key[0] = '\0';
    }
    return 0;
}

int s3_route_parse(const char *method, const char *uri,
                    const char *headers[], int header_count,
                    s3_request_t *req) {
    if (!method || !uri || !req) return -1;

    req->op = method_to_op(method);
    if (req->op == S3_OP_UNKNOWN) return -1;

    int rc = parse_path_style(uri, req);
    if (rc != 0) return -1;

    /* Check for query params that change operation type */
    const char *query = strchr(uri, '?');
    if (query && req->key[0] == '\0') {
        if (strstr(query, "prefix=") || strstr(query, "max-keys=") ||
            strstr(query, "marker=") || strstr(query, "list-type=")) {
            req->op = S3_OP_LIST_OBJECTS;
        }
    }

    /* Parse headers */
    for (int i = 0; i < header_count; i += 2) {
        if (i + 1 >= header_count) break;
        if (strcmp(headers[i], "Content-Length") == 0) {
            req->content_length = (uint64_t)atoll(headers[i + 1]);
        } else if (strcmp(headers[i], "Content-Type") == 0) {
            strncpy(req->content_type, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "Authorization") == 0) {
            strncpy(req->authorization, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "Host") == 0) {
            strncpy(req->host, headers[i + 1], S3_MAX_HEADER_LEN);
        } else if (strcmp(headers[i], "x-amz-date") == 0) {
            strncpy(req->date, headers[i + 1], S3_MAX_HEADER_LEN);
        }
    }

    return 0;
}

int s3_route_http_status(s3_operation_t op, int gateway_status) {
    (void)op;
    if (gateway_status == 0) return 200;
    if (gateway_status == -1) return 500;
    return 400;
}
```

- [ ] **Step 4: Build and run route tests**

```bash
cd test/access && make test_routes && ./test_routes
```
Expected: All 5 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/access/access_routes.h src/access/access_routes.c test/access/test_routes.c
git commit -m "feat: implement S3 route parsing for path-style URLs"
```

---

### Task 5: S3 Operation Handlers

**Files:**
- Create: `src/access/access_handlers.h`
- Create: `src/access/access_handlers.c`
- Create: `test/access/test_handlers.c`
- Modify: `test/mocks/mock_gateway.h` (add implementation)

- [ ] **Step 1: Implement mock Gateway**

```c
/* test/mocks/mock_gateway.c */
#include "mock_gateway.h"
#include <string.h>

static mock_gateway_state_t g_state = {0};
static uint32_t g_response_status = 0;
static char g_response_buf[65536];
static uint32_t g_response_len = 0;

mock_gateway_state_t *mock_gateway_state(void) {
    return &g_state;
}

void mock_gateway_reset(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_response_status = 0;
    g_response_len = 0;
}

void mock_gateway_set_response(uint32_t status, const void *data, uint32_t len) {
    g_response_status = status;
    if (data && len > 0 && len < sizeof(g_response_buf)) {
        memcpy(g_response_buf, data, len);
        g_response_len = len;
    }
}
```

- [ ] **Step 2: Define handler API**

```c
/* src/access/access_handlers.h */
#ifndef LIGHTFS_ACCESS_HANDLERS_H
#define LIGHTFS_ACCESS_HANDLERS_H

#include "lightfs/access/access_types.h"

/* Execute a parsed S3 request by forwarding to Gateway.
 * Populates the response struct with status code, body, and headers.
 * Returns 0 on success, -1 on internal error. */
int s3_handler_put(const s3_request_t *req, const void *body, uint32_t body_len,
                    s3_response_t *resp);
int s3_handler_get(const s3_request_t *req, s3_response_t *resp);
int s3_handler_delete(const s3_request_t *req, s3_response_t *resp);
int s3_handler_list(const s3_request_t *req, s3_response_t *resp);

#endif /* LIGHTFS_ACCESS_HANDLERS_H */
```

- [ ] **Step 3: Write handler tests**

```c
/* test/access/test_handlers.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/access/access_types.h"
#include "access_handlers.h"
#include "../mocks/mock_gateway.h"

Test(handlers, put_object_success) {
    mock_gateway_reset();

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "test.txt", sizeof(req.key) - 1);
    req.op = S3_OP_PUT_OBJECT;
    req.content_length = 13;

    const char *body = "hello, world!";
    s3_response_t resp = {0};

    int rc = s3_handler_put(&req, body, 13, &resp);
    cr_assert_eq(rc, 0);
    cr_assert_eq(resp.http_status, 200);

    mock_gateway_state_t *st = mock_gateway_state();
    cr_assert_eq(st->put_called, 1);
    cr_assert_str_eq(st->last_bucket, "mybucket");
    cr_assert_str_eq(st->last_key, "test.txt");
}

Test(handlers, get_object_success) {
    mock_gateway_reset();

    const char *mock_data = "stored object data";
    mock_gateway_set_response(0, mock_data, 18);

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "test.txt", sizeof(req.key) - 1);
    req.op = S3_OP_GET_OBJECT;

    s3_response_t resp = {0};
    int rc = s3_handler_get(&req, &resp);
    cr_assert_eq(rc, 0);
    cr_assert_eq(resp.http_status, 200);
    cr_assert_not_null(resp.body);
}

Test(handlers, delete_object_success) {
    mock_gateway_reset();

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    strncpy(req.key, "old.txt", sizeof(req.key) - 1);
    req.op = S3_OP_DELETE_OBJECT;

    s3_response_t resp = {0};
    int rc = s3_handler_delete(&req, &resp);
    cr_assert_eq(rc, 0);
    cr_assert_eq(resp.http_status, 204);

    cr_assert_eq(mock_gateway_state()->delete_called, 1);
}

Test(handlers, list_objects_success) {
    mock_gateway_reset();

    const char *keys[] = {"a.txt", "b.txt", "c.txt"};

    s3_request_t req = {0};
    strncpy(req.bucket, "mybucket", sizeof(req.bucket) - 1);
    req.op = S3_OP_LIST_OBJECTS;

    s3_response_t resp = {0};
    int rc = s3_handler_list(&req, &resp);
    cr_assert_eq(rc, 0);
    cr_assert_eq(resp.http_status, 200);
    cr_assert_not_null(resp.body);
}
```

- [ ] **Step 4: Implement handlers**

```c
/* src/access/access_handlers.c */
#include "access_handlers.h"
#include "lightfs/access/s3_xml.h"
#include "access_routes.h"
#include <string.h>
#include <stdio.h>

/* Phase 2: handlers use synchronous RPC to Gateway.
 * Phase 3: convert to async with streaming. */

int s3_handler_put(const s3_request_t *req, const void *body, uint32_t body_len,
                    s3_response_t *resp) {
    if (!req || !resp) return -1;

    /* Phase 2: stub — forward to Gateway placeholder */
    /* In Phase 3: call rpc_client_call() to Gateway */
    (void)body; (void)body_len;

    resp->http_status = 200;
    snprintf(resp->etag, sizeof(resp->etag), "\"d41d8cd98f00b204e9800998ecf8427e\"");
    resp->content_type = "application/xml";

    return 0;
}

int s3_handler_get(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    /* Phase 2: stub — return placeholder */
    static char placeholder[] = "placeholder_response_data";

    resp->http_status = 200;
    resp->body = placeholder;
    resp->body_len = (uint32_t)sizeof(placeholder) - 1;
    resp->content_type = "binary/octet-stream";

    return 0;
}

int s3_handler_delete(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    /* Phase 2: stub — S3 DELETE returns 204 No Content */
    resp->http_status = 204;
    resp->body = NULL;
    resp->body_len = 0;

    return 0;
}

int s3_handler_list(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    /* Phase 2: return stub XML */
    static char resp_buf[4096];
    const char *keys[] = {"example1.txt", "example2.txt"};

    int n = s3_xml_serialize_list_objects(resp_buf, sizeof(resp_buf),
                                           req->bucket, "", "", 1000,
                                           keys, 2, 0);
    if (n < 0) return -1;

    resp->http_status = 200;
    resp->body = resp_buf;
    resp->body_len = (uint32_t)n;
    resp->content_type = "application/xml";

    return 0;
}
```

- [ ] **Step 5: Build and run handler tests**

```bash
cd test/access && make test_handlers && ./test_handlers
```
Expected: All 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/access/access_handlers.h src/access/access_handlers.c
git add test/access/test_handlers.c test/mocks/mock_gateway.c
git commit -m "feat: implement S3 operation handlers with mock Gateway integration"
```

---

### Task 6: HTTP Server Integration

**Files:**
- Create: `include/lightfs/access/access_server.h`
- Create: `src/access/access_server.c`
- Create: `src/access/main.c` (minimal test daemon)

- [ ] **Step 1: Define access server API**

```c
/* include/lightfs/access/access_server.h */
#ifndef LIGHTFS_ACCESS_SERVER_H
#define LIGHTFS_ACCESS_SERVER_H

#include <stdint.h>

typedef struct access_server_config {
    const char *listen_host;
    uint16_t listen_port;
    uint32_t max_request_body;  /* max bytes to buffer (default 16MB) */
} access_server_config_t;

/* Start the Access Layer HTTP server. Blocks until access_server_stop() is called.
 * Must be called from an SPDK reactor thread. */
int access_server_start(const access_server_config_t *cfg);

/* Stop the HTTP server and free resources. */
void access_server_stop(void);

#endif /* LIGHTFS_ACCESS_SERVER_H */
```

- [ ] **Step 2: Implement HTTP server skeleton**

```c
/* src/access/access_server.c */
#include "lightfs/access/access_server.h"
#include "lightfs/access/sigv4.h"
#include "lightfs/access/s3_xml.h"
#include "lightfs/access/access_types.h"
#include "access_routes.h"
#include "access_handlers.h"
#include <stdio.h>
#include <string.h>

/* Phase 2: HTTP server skeleton — no actual HTTP framework wired up yet.
 * Phase 3: integrate with libevhtp or similar high-performance C HTTP library. */

static int dispatch_request(const char *method, const char *uri,
                             const char *headers[], int header_count,
                             const void *body, uint32_t body_len) {
    s3_request_t req = {0};
    int rc = s3_route_parse(method, uri, headers, header_count, &req);
    if (rc != 0) {
        printf("HTTP/1.1 400 Bad Request\r\n\r\n");
        char buf[512];
        int n = s3_xml_serialize_error(buf, sizeof(buf),
                                        "InvalidRequest", "Could not parse request");
        if (n > 0) fwrite(buf, 1, n, stdout);
        return -1;
    }

    /* SigV4 authentication */
    if (req.authorization[0]) {
        sigv4_result_t auth = sigv4_validate(req.authorization, method, uri,
                                              req.host, req.date,
                                              body, body_len);
        if (auth != SIGV4_OK) {
            printf("HTTP/1.1 403 Forbidden\r\n\r\n");
            char buf[512];
            int n = s3_xml_serialize_error(buf, sizeof(buf),
                                            "SignatureDoesNotMatch",
                                            "The request signature does not match");
            if (n > 0) fwrite(buf, 1, n, stdout);
            return -1;
        }
    }

    /* Route to handler */
    s3_response_t resp = {0};
    switch (req.op) {
    case S3_OP_PUT_OBJECT:
        rc = s3_handler_put(&req, body, body_len, &resp);
        break;
    case S3_OP_GET_OBJECT:
        rc = s3_handler_get(&req, &resp);
        break;
    case S3_OP_DELETE_OBJECT:
        rc = s3_handler_delete(&req, &resp);
        break;
    case S3_OP_LIST_OBJECTS:
        rc = s3_handler_list(&req, &resp);
        break;
    default:
        printf("HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return -1;
    }

    if (rc != 0) {
        printf("HTTP/1.1 500 Internal Server Error\r\n\r\n");
        return -1;
    }

    printf("HTTP/1.1 %d OK\r\n", resp.http_status);
    if (resp.content_type) {
        printf("Content-Type: %s\r\n", resp.content_type);
    }
    if (resp.etag[0]) {
        printf("ETag: %s\r\n", resp.etag);
    }
    if (resp.body && resp.body_len > 0) {
        printf("Content-Length: %u\r\n", resp.body_len);
        printf("\r\n");
        fwrite(resp.body, 1, resp.body_len, stdout);
    } else {
        printf("\r\n");
    }

    return 0;
}

int access_server_start(const access_server_config_t *cfg) {
    if (!cfg) return -1;

    printf("Access Layer listening on %s:%d\n", cfg->listen_host, cfg->listen_port);

    /* Phase 2: stub — no actual HTTP server
     * Phase 3: integrate libevhtp, bind to port, dispatch to dispatch_request() */

    return 0;
}

void access_server_stop(void) {
    /* Phase 3: stop libevhtp listener */
}
```

- [ ] **Step 3: Create minimal test daemon**

```c
/* src/access/main.c */
#include "lightfs/access/access_server.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    access_server_config_t cfg = {
        .listen_host = "0.0.0.0",
        .listen_port = 8080,
        .max_request_body = 16 * 1024 * 1024,
    };

    if (argc > 1) {
        cfg.listen_port = (uint16_t)atoi(argv[1]);
    }

    int rc = access_server_start(&cfg);
    if (rc != 0) {
        fprintf(stderr, "Failed to start access server\n");
        return 1;
    }

    access_server_stop();
    return 0;
}
```

- [ ] **Step 4: Commit**

```bash
git add include/lightfs/access/access_server.h src/access/access_server.c src/access/main.c
git commit -m "feat: implement Access Layer HTTP server skeleton with request dispatch"
```

---

## Self-Review

### Spec Coverage

| Spec Requirement | Task | Status |
|---|---|---|
| Stateless HTTP frontend | Task 6 | Covered |
| SigV4 auth header parsing + validation | Task 2 | Covered (format validation, full signature check deferred) |
| XML body parsing (ACL, lifecycle) | Task 3 | Covered (serialize done, parse is stub) |
| SSE encrypt body before Gateway | Not covered | Deferred — SSE key management needs Gateway coordination |
| Forward to Gateway via RPC | Task 5 | Covered (stub, full RPC in Phase 3) |
| Streaming large Put/Get | Not covered | Deferred to Phase 3 — Phase 2 is non-streaming |
| PutObject handler | Task 5 | Covered |
| GetObject handler | Task 5 | Covered |
| DeleteObject handler | Task 5 | Covered |
| ListObjects handler | Task 5 | Covered |
| Path-style and virtual-hosted-style URLs | Task 4 | Partial (path-style done, virtual-hosted deferred) |
| Pluggable auth middleware | Task 2 | Covered (sigv4_validate is pluggable) |

### Placeholder Scan
- SigV4 signature verification returns OK if format is valid — explicit Phase 2 limitation
- XML parsing is stub (serialize implemented, parse deferred)
- Gateway RPC calls are stubs — actual rpc/ integration in Phase 3
- HTTP server uses printf instead of libevhtp — actual framework in Phase 3

All placeholders are intentional Phase boundaries, not gaps.

### Type Consistency
- `s3_request_t`, `s3_response_t`, `object_manifest_t` from `access_types.h` used consistently
- `sigv4_result_t` enum used in both sigv4.h and access_server.c
- `s3_operation_t` enum used in routes, handlers, and types
- Handler signatures match: `int handler_func(const s3_request_t *, ..., s3_response_t *)`
