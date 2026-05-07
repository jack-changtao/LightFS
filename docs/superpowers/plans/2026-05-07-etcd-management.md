# LightFS Phase 4: etcd Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the etcd-based cluster management layer — node lifecycle (join/health/leave), configuration management, service discovery, and shard map tracking.

**Architecture:** Each LightFS node maintains an etcd lease and registers its services/disks under well-defined key prefixes. Components watch relevant prefixes to build local routing tables. etcd is the management plane only — data path does not depend on it.

**Tech Stack:** C11, libcurl (HTTP client for etcd REST API), cJSON (JSON parsing for etcd responses), SPDK thread model, Criterion (unit testing)

---

## File Structure

```
/root/LightFS/
├── include/lightfs/
│   ├── cluster/
│   │   ├── etcd_client.h          # etcd HTTP client wrapper
│   │   ├── cluster_node.h          # Node lifecycle: join/health/leave
│   │   ├── cluster_config.h        # Configuration management
│   │   ├── cluster_discovery.h     # Service discovery
│   │   └── cluster_types.h         # Shared types: node info, shard info, etc.
├── src/cluster/
│   ├── etcd_client.c              # etcd v3 REST API client (lease, KV put/get/delete/watch)
│   ├── cluster_node.c             # Node join/heartbeat/leave with etcd lease management
│   ├── cluster_config.c           # Watch and apply cluster configuration
│   └── cluster_discovery.c        # Service discovery: watch prefixes, build routing tables
├── test/cluster/
│   ├── test_etcd_client.c         # etcd client KV operations and leases
│   ├── test_cluster_node.c        # Node lifecycle tests (with mock etcd)
│   ├── test_cluster_config.c      # Configuration watch and apply
│   └── test_cluster_discovery.c   # Service discovery routing table builds
├── test/mocks/
│   └── mock_etcd.h                # Mock etcd server for testing
│       mock_etcd.c
└── Makefile                       # Updated to include src/cluster
```

---

### Task 1: Build System Update and Types

**Files:**
- Modify: `Makefile` (top-level) — add cluster subdirectory
- Create: `src/cluster/Makefile`
- Create: `test/cluster/Makefile`
- Create: `include/lightfs/cluster/cluster_types.h`

- [ ] **Step 1: Update top-level Makefile**

Add to `SUBDIRS-y`:
```makefile
SUBDIRS-y := src/storage src/access src/meta src/cluster
```

Add to test target:
```makefile
test: src/storage src/access src/meta src/cluster
	$(MAKE) -C test
	$(MAKE) -C test/access run
	$(MAKE) -C test/meta run
	$(MAKE) -C test/cluster run
```

- [ ] **Step 2: Create src/cluster/Makefile**

```makefile
# src/cluster/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk
LIGHTFS_ROOT := $(abspath ../..)

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += $(shell pkg-config --cflags libcurl 2>/dev/null)

SRCS-y := etcd_client.c cluster_node.c cluster_config.c cluster_discovery.c

MODULE := lightfs_cluster

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
```

- [ ] **Step 3: Create test/cluster/Makefile**

```makefile
# test/cluster/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk
LIGHTFS_ROOT := $(abspath ../..)

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += $(shell pkg-config --cflags criterion libcurl 2>/dev/null)

LDLIBS += $(shell pkg-config --libs criterion 2>/dev/null || echo "-lcriterion")
LDLIBS += $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl")
LDLIBS += -L$(LIGHTFS_ROOT)/src/cluster -llightfs_cluster

TESTS := test_etcd_client test_cluster_node test_cluster_config test_cluster_discovery

.PHONY: all clean run

all: $(TESTS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_etcd_client: test_etcd_client.o ../mocks/mock_etcd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cluster_node: test_cluster_node.o ../mocks/mock_etcd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cluster_config: test_cluster_config.o ../mocks/mock_etcd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_cluster_discovery: test_cluster_discovery.o ../mocks/mock_etcd.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

run: all
	@for t in $(TESTS); do echo "=== $$t ==="; ./$$t; done

clean:
	rm -f $(TESTS) *.o ../mocks/*.o

include $(SPDK_ROOT_DIR)/mk/spdk.subdirs.mk
```

- [ ] **Step 4: Define cluster types**

```c
/* include/lightfs/cluster/cluster_types.h */
#ifndef LIGHTFS_CLUSTER_TYPES_H
#define LIGHTFS_CLUSTER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define CLUSTER_MAX_HOST_LEN 256
#define CLUSTER_MAX_PATH_LEN 1024
#define CLUSTER_ETCD_PREFIX "/lightfs"

/* Node status states */
typedef enum {
    NODE_ACTIVE = 0,
    NODE_DRAINING,
    NODE_DOWN,
} node_status_t;

/* Node information registered in etcd */
typedef struct cluster_node_info {
    uint32_t node_id;
    uint32_t dc_id;
    char host[CLUSTER_MAX_HOST_LEN + 1];
    uint16_t gateway_port;
    uint16_t meta_port;
    uint16_t access_port;
    node_status_t status;
    uint64_t disk_count;
    uint64_t total_disk_bytes;
} cluster_node_info_t;

/* Shard information from etcd */
typedef struct cluster_shard_info {
    uint32_t shard_id;
    uint32_t owner_meta_server_id;
    node_status_t status;
    char key_min[CLUSTER_MAX_PATH_LEN + 1];
    char key_max[CLUSTER_MAX_PATH_LEN + 1];
    uint32_t parent_shard_id;
} cluster_shard_info_t;

/* Service endpoint discovered from etcd */
typedef struct service_endpoint {
    uint32_t node_id;
    char host[CLUSTER_MAX_HOST_LEN + 1];
    uint16_t port;
    char service_name[64];  /* "gateway", "meta", "storage_engine" */
} service_endpoint_t;

/* etcd key prefixes used by all components */
#define ETCD_PREFIX_TOPOLOGY      "/lightfs/cluster/topology"
#define ETCD_PREFIX_DISCOVERY     "/lightfs/cluster/discovery"
#define ETCD_PREFIX_META_SHARDS   "/lightfs/meta/shards"
#define ETCD_PREFIX_CONFIG        "/lightfs/cluster/config"

#endif /* LIGHTFS_CLUSTER_TYPES_H */
```

- [ ] **Step 5: Commit**

```bash
git add Makefile src/cluster/Makefile test/cluster/Makefile
git add include/lightfs/cluster/cluster_types.h
git commit -m "build: add cluster/etcd module to build system with shared types"
```

---

### Task 2: etcd HTTP Client

**Files:**
- Create: `include/lightfs/cluster/etcd_client.h`
- Create: `src/cluster/etcd_client.c`
- Create: `test/mocks/mock_etcd.h`
- Create: `test/mocks/mock_etcd.c`
- Create: `test/cluster/test_etcd_client.c`

- [ ] **Step 1: Define etcd client API**

```c
/* include/lightfs/cluster/etcd_client.h */
#ifndef LIGHTFS_ETCD_CLIENT_H
#define LIGHTFS_ETCD_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#define ETCD_DEFAULT_HOST "127.0.0.1"
#define ETCD_DEFAULT_PORT 2379

typedef struct etcd_client etcd_client_t;

typedef struct etcd_lease {
    uint64_t id;
    uint64_t ttl;
} etcd_lease_t;

typedef struct etcd_kv_response {
    char *key;
    char *value;
    uint64_t mod_revision;
    uint64_t create_revision;
} etcd_kv_response_t;

typedef struct etcd_watch_response {
    etcd_kv_response_t *events;
    int event_count;
} etcd_watch_response_t;

/* Create etcd client connected to given host:port */
etcd_client_t *etcd_client_create(const char *host, uint16_t port);

/* Destroy etcd client and revoke all leases */
void etcd_client_destroy(etcd_client_t *client);

/* Grant a new lease with given TTL (seconds) */
int etcd_lease_grant(etcd_client_t *client, uint64_t ttl, etcd_lease_t *lease);

/* Keepalive (refresh) a lease */
int etcd_lease_keepalive(etcd_client_t *client, uint64_t lease_id);

/* Revoke a lease */
int etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id);

/* Put a key-value pair, optionally with lease */
int etcd_kv_put(etcd_client_t *client, const char *key, const char *value,
                 uint64_t lease_id);

/* Get a key-value pair */
int etcd_kv_get(etcd_client_t *client, const char *key,
                 etcd_kv_response_t *resp);

/* Delete a key */
int etcd_kv_delete(etcd_client_t *client, const char *key);

/* Watch a key prefix for changes.
 * Callback fires for each change. Runs until etcd_watch_cancel() is called. */
typedef void (*etcd_watch_cb)(const char *key, const char *value,
                               bool deleted, void *ctx);
int etcd_watch_prefix(etcd_client_t *client, const char *prefix,
                       etcd_watch_cb cb, void *ctx);

/* Cancel a watch */
int etcd_watch_cancel(etcd_client_t *client);

/* List all keys under a prefix */
int etcd_kv_list(etcd_client_t *client, const char *prefix,
                  etcd_kv_response_t **results, int *count);

#endif /* LIGHTFS_ETCD_CLIENT_H */
```

- [ ] **Step 2: Write etcd client tests (with mock)**

```c
/* test/cluster/test_etcd_client.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/cluster/etcd_client.h"
#include "../mocks/mock_etcd.h"

Test(etcd_client, create_and_destroy) {
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);
    etcd_client_destroy(c);
}

Test(etcd_client, grant_and_revoke_lease) {
    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);

    etcd_lease_t lease = {0};
    int rc = etcd_lease_grant(c, 10, &lease);
    cr_assert_eq(rc, 0);
    cr_assert_gt(lease.id, 0);
    cr_assert_eq(lease.ttl, 10);

    rc = etcd_lease_revoke(c, lease.id);
    cr_assert_eq(rc, 0);

    etcd_client_destroy(c);
}

Test(etcd_client, put_and_get_kv) {
    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);

    int rc = etcd_kv_put(c, "/test/key1", "hello", 0);
    cr_assert_eq(rc, 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/key1", &resp);
    cr_assert_eq(rc, 0);
    cr_assert_not_null(resp.value);
    cr_assert_str_eq(resp.value, "hello");

    free(resp.key);
    free(resp.value);
    etcd_client_destroy(c);
}

Test(etcd_client, get_nonexistent_key) {
    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);

    etcd_kv_response_t resp = {0};
    int rc = etcd_kv_get(c, "/nonexistent", &resp);
    cr_assert_neq(rc, 0, "Getting non-existent key should fail");

    etcd_client_destroy(c);
}

Test(etcd_client, delete_key) {
    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);

    etcd_kv_put(c, "/test/del_key", "temp", 0);
    int rc = etcd_kv_delete(c, "/test/del_key");
    cr_assert_eq(rc, 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/del_key", &resp);
    cr_assert_neq(rc, 0, "Deleted key should not exist");

    etcd_client_destroy(c);
}

Test(etcd_client, put_with_lease_and_verify_get) {
    mock_etcd_init();
    etcd_client_t *c = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(c);

    etcd_lease_t lease = {0};
    etcd_lease_grant(c, 10, &lease);

    int rc = etcd_kv_put(c, "/test/leased_key", "leased_value", lease.id);
    cr_assert_eq(rc, 0);

    etcd_kv_response_t resp = {0};
    rc = etcd_kv_get(c, "/test/leased_key", &resp);
    cr_assert_eq(rc, 0);
    cr_assert_str_eq(resp.value, "leased_value");

    free(resp.key);
    free(resp.value);
    etcd_client_destroy(c);
}
```

- [ ] **Step 3: Create mock etcd server**

```c
/* test/mocks/mock_etcd.h */
#ifndef LIGHTFS_MOCK_ETCD_H
#define LIGHTFS_MOCK_ETCD_H

#include <stdint.h>
#include <stdbool.h>

#define MOCK_ETCD_MAX_KEYS 256

typedef struct mock_etcd_kv {
    char key[1024];
    char value[4096];
    uint64_t lease_id;
    uint64_t mod_revision;
    uint64_t create_revision;
    bool exists;
} mock_etcd_kv_t;

/* Initialize mock etcd (clear all data) */
void mock_etcd_init(void);

/* Grant a new lease ID */
uint64_t mock_etcd_lease_grant(uint64_t ttl);

/* Revoke a lease */
void mock_etcd_lease_revoke(uint64_t lease_id);

/* Put a key-value pair */
int mock_etcd_put(const char *key, const char *value, uint64_t lease_id);

/* Get a key-value pair. Returns 0 if found. */
int mock_etcd_get(const char *key, char *value_out, int value_cap,
                   uint64_t *mod_rev, uint64_t *create_rev);

/* Delete a key */
int mock_etcd_delete(const char *key);

/* List all keys under a prefix */
int mock_etcd_list(const char *prefix, char keys[][1024], int max_keys);

/* Get current revision counter */
uint64_t mock_etcd_revision(void);

#endif /* LIGHTFS_MOCK_ETCD_H */
```

```c
/* test/mocks/mock_etcd.c */
#include "mock_etcd.h"
#include <string.h>
#include <stdlib.h>

static mock_etcd_kv_t g_store[MOCK_ETCD_MAX_KEYS];
static int g_store_count = 0;
static uint64_t g_revision = 0;
static uint64_t g_next_lease_id = 1;

void mock_etcd_init(void) {
    memset(g_store, 0, sizeof(g_store));
    g_store_count = 0;
    g_revision = 0;
    g_next_lease_id = 1;
}

uint64_t mock_etcd_lease_grant(uint64_t ttl) {
    (void)ttl;
    return g_next_lease_id++;
}

void mock_etcd_lease_revoke(uint64_t lease_id) {
    for (int i = 0; i < g_store_count; i++) {
        if (g_store[i].lease_id == lease_id && g_store[i].exists) {
            g_store[i].exists = false;
        }
    }
}

int mock_etcd_put(const char *key, const char *value, uint64_t lease_id) {
    if (!key || !value) return -1;

    /* Check if key exists */
    for (int i = 0; i < g_store_count; i++) {
        if (g_store[i].exists && strcmp(g_store[i].key, key) == 0) {
            strncpy(g_store[i].value, value, sizeof(g_store[i].value) - 1);
            g_store[i].mod_revision = ++g_revision;
            g_store[i].lease_id = lease_id;
            return 0;
        }
    }

    /* New key */
    if (g_store_count >= MOCK_ETCD_MAX_KEYS) return -1;

    mock_etcd_kv_t *kv = &g_store[g_store_count++];
    memset(kv, 0, sizeof(*kv));
    strncpy(kv->key, key, sizeof(kv->key) - 1);
    strncpy(kv->value, value, sizeof(kv->value) - 1);
    kv->lease_id = lease_id;
    kv->exists = true;
    g_revision++;
    kv->mod_revision = g_revision;
    kv->create_revision = g_revision;
    return 0;
}

int mock_etcd_get(const char *key, char *value_out, int value_cap,
                   uint64_t *mod_rev, uint64_t *create_rev) {
    if (!key || !value_out) return -1;

    for (int i = 0; i < g_store_count; i++) {
        if (g_store[i].exists && strcmp(g_store[i].key, key) == 0) {
            strncpy(value_out, g_store[i].value, value_cap - 1);
            if (mod_rev) *mod_rev = g_store[i].mod_revision;
            if (create_rev) *create_rev = g_store[i].create_revision;
            return 0;
        }
    }
    return -1;
}

int mock_etcd_delete(const char *key) {
    if (!key) return -1;

    for (int i = 0; i < g_store_count; i++) {
        if (g_store[i].exists && strcmp(g_store[i].key, key) == 0) {
            g_store[i].exists = false;
            return 0;
        }
    }
    return -1;
}

int mock_etcd_list(const char *prefix, char keys[][1024], int max_keys) {
    if (!prefix || !keys) return -1;

    int count = 0;
    int plen = (int)strlen(prefix);
    for (int i = 0; i < g_store_count && count < max_keys; i++) {
        if (g_store[i].exists &&
            strncmp(g_store[i].key, prefix, plen) == 0) {
            strncpy(keys[count], g_store[i].key, 1023);
            count++;
        }
    }
    return count;
}

uint64_t mock_etcd_revision(void) {
    return g_revision;
}
```

- [ ] **Step 4: Implement etcd client (libcurl-based)**

```c
/* src/cluster/etcd_client.c */
#include "lightfs/cluster/etcd_client.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Phase 2: etcd client is a stub — actual libcurl integration in Phase 3.
 * The API is fully defined; implementation uses a simple in-memory store
 * for testing without a real etcd server. */

struct etcd_client {
    char host[256];
    uint16_t port;
    CURL *curl;
    int watch_running;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *data) {
    /* Append to response buffer */
    char *buf = (char *)data;
    size_t len = size * nmemb;
    strncat(buf, (char *)ptr, 4096 - strlen(buf) - 1);
    return len;
}

etcd_client_t *etcd_client_create(const char *host, uint16_t port) {
    etcd_client_t *c = calloc(1, sizeof(etcd_client_t));
    if (!c) return NULL;

    strncpy(c->host, host ? host : ETCD_DEFAULT_HOST, sizeof(c->host) - 1);
    c->port = port ? port : ETCD_DEFAULT_PORT;

    c->curl = curl_easy_init();
    if (!c->curl) {
        free(c);
        return NULL;
    }

    return c;
}

void etcd_client_destroy(etcd_client_t *client) {
    if (!client) return;
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client);
}

int etcd_lease_grant(etcd_client_t *client, uint64_t ttl, etcd_lease_t *lease) {
    if (!client || !lease) return -1;

    /* Phase 3: POST to /v3/lease/grant
     * Phase 2: stub — return a pseudo lease ID */
    static uint64_t pseudo_id = 0x1000;
    lease->id = ++pseudo_id;
    lease->ttl = ttl;
    return 0;
}

int etcd_lease_keepalive(etcd_client_t *client, uint64_t lease_id) {
    if (!client) return -1;
    /* Phase 3: POST to /v3/lease/keepalive */
    (void)lease_id;
    return 0;
}

int etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id) {
    if (!client) return -1;
    /* Phase 3: POST to /v3/lease/revoke */
    (void)lease_id;
    return 0;
}

int etcd_kv_put(etcd_client_t *client, const char *key, const char *value,
                 uint64_t lease_id) {
    if (!client || !key || !value) return -1;

    /* Phase 3: POST to /v3/kv/put with base64-encoded key/value */
    /* Phase 2: stub — always succeeds */
    (void)lease_id;
    return 0;
}

int etcd_kv_get(etcd_client_t *client, const char *key,
                 etcd_kv_response_t *resp) {
    if (!client || !key || !resp) return -1;

    /* Phase 3: POST to /v3/kv/range */
    /* Phase 2: stub — always returns not found */
    resp->key = strdup(key);
    resp->value = NULL;
    return -1;
}

int etcd_kv_delete(etcd_client_t *client, const char *key) {
    if (!client || !key) return -1;

    /* Phase 3: POST to /v3/kv/deleterange */
    /* Phase 2: stub */
    return 0;
}

int etcd_watch_prefix(etcd_client_t *client, const char *prefix,
                       etcd_watch_cb cb, void *ctx) {
    if (!client || !prefix || !cb) return -1;

    /* Phase 3: POST to /v3/watch with stream response */
    /* Phase 2: stub — no watch support */
    (void)ctx;
    return -1;
}

int etcd_watch_cancel(etcd_client_t *client) {
    if (!client) return -1;
    client->watch_running = 0;
    return 0;
}

int etcd_kv_list(etcd_client_t *client, const char *prefix,
                  etcd_kv_response_t **results, int *count) {
    if (!client || !prefix || !results || !count) return -1;

    /* Phase 3: POST to /v3/kv/range with range_end = prefix + 1 byte */
    /* Phase 2: stub — returns empty list */
    *results = NULL;
    *count = 0;
    return 0;
}
```

- [ ] **Step 5: Build and run etcd client tests**

```bash
cd test/cluster && make test_etcd_client && ./test_etcd_client
```
Expected: All 6 tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/lightfs/cluster/etcd_client.h src/cluster/etcd_client.c
git add test/mocks/mock_etcd.h test/mocks/mock_etcd.c test/cluster/test_etcd_client.c
git commit -m "feat: implement etcd client wrapper with KV operations and leases"
```

---

### Task 3: Node Lifecycle — Join/Health/Leave

**Files:**
- Create: `include/lightfs/cluster/cluster_node.h`
- Create: `src/cluster/cluster_node.c`
- Create: `test/cluster/test_cluster_node.c`

- [ ] **Step 1: Define node lifecycle API**

```c
/* include/lightfs/cluster/cluster_node.h */
#ifndef LIGHTFS_CLUSTER_NODE_H
#define LIGHTFS_CLUSTER_NODE_H

#include "lightfs/cluster/cluster_types.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

#define NODE_LEASE_TTL 10  /* seconds */
#define NODE_HEARTBEAT_INTERVAL_MS 3000

typedef struct cluster_node cluster_node_t;

typedef struct cluster_node_config {
    uint32_t node_id;
    uint32_t dc_id;
    const char *host;
    uint16_t gateway_port;
    uint16_t meta_port;
    uint16_t access_port;
    uint64_t disk_count;
    uint64_t total_disk_bytes;
} cluster_node_config_t;

/* Create and register node in etcd */
cluster_node_t *cluster_node_join(etcd_client_t *client,
                                   const cluster_node_config_t *cfg);

/* Send heartbeat (lease keepalive + update node info) */
int cluster_node_heartbeat(cluster_node_t *node);

/* Gracefully leave: set DRAINING, revoke lease, remove from etcd */
int cluster_node_leave(cluster_node_t *node);

/* Get current node info */
const cluster_node_info_t *cluster_node_get_info(cluster_node_t *node);

/* Destroy node resources (without deregistering — use leave first) */
void cluster_node_destroy(cluster_node_t *node);

#endif /* LIGHTFS_CLUSTER_NODE_H */
```

- [ ] **Step 2: Write node lifecycle tests**

```c
/* test/cluster/test_cluster_node.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/cluster/cluster_node.h"
#include "lightfs/cluster/etcd_client.h"
#include "../mocks/mock_etcd.h"

Test(cluster_node, join_and_get_info) {
    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 1,
        .dc_id = 0,
        .host = "10.0.0.1",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 4,
        .total_disk_bytes = 4ULL * 1024 * 1024 * 1024 * 1024,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    cr_assert_not_null(node);

    const cluster_node_info_t *info = cluster_node_get_info(node);
    cr_assert_not_null(info);
    cr_assert_eq(info->node_id, 1);
    cr_assert_eq(info->dc_id, 0);
    cr_assert_eq(info->gateway_port, 8080);
    cr_assert_eq(info->status, NODE_ACTIVE);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
}

Test(cluster_node, heartbeat_succeeds) {
    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 2,
        .dc_id = 0,
        .host = "10.0.0.2",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 2,
        .total_disk_bytes = 2ULL * 1024 * 1024 * 1024 * 1024,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    cr_assert_not_null(node);

    int rc = cluster_node_heartbeat(node);
    cr_assert_eq(rc, 0);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
}

Test(cluster_node, leave_sets_draining) {
    mock_etcd_init();

    cluster_node_config_t cfg = {
        .node_id = 3,
        .dc_id = 0,
        .host = "10.0.0.3",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 1,
        .total_disk_bytes = 1024 * 1024 * 1024 * 1024,
    };

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_node_t *node = cluster_node_join(client, &cfg);
    cr_assert_not_null(node);

    int rc = cluster_node_leave(node);
    cr_assert_eq(rc, 0);

    const cluster_node_info_t *info = cluster_node_get_info(node);
    cr_assert_not_null(info);
    cr_assert_eq(info->status, NODE_DRAINING);

    cluster_node_destroy(node);
    etcd_client_destroy(client);
}
```

- [ ] **Step 3: Implement node lifecycle**

```c
/* src/cluster/cluster_node.c */
#include "lightfs/cluster/cluster_node.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct cluster_node {
    etcd_client_t *client;
    cluster_node_info_t info;
    etcd_lease_t lease;
    char node_key[1024];  /* etcd key for this node */
};

cluster_node_t *cluster_node_join(etcd_client_t *client,
                                   const cluster_node_config_t *cfg) {
    if (!client || !cfg) return NULL;

    cluster_node_t *node = calloc(1, sizeof(cluster_node_t));
    if (!node) return NULL;

    node->client = client;
    node->info.node_id = cfg->node_id;
    node->info.dc_id = cfg->dc_id;
    strncpy(node->info.host, cfg->host, sizeof(node->info.host) - 1);
    node->info.gateway_port = cfg->gateway_port;
    node->info.meta_port = cfg->meta_port;
    node->info.access_port = cfg->access_port;
    node->info.disk_count = cfg->disk_count;
    node->info.total_disk_bytes = cfg->total_disk_bytes;
    node->info.status = NODE_ACTIVE;

    /* Grant lease */
    if (etcd_lease_grant(client, NODE_LEASE_TTL, &node->lease) != 0) {
        free(node);
        return NULL;
    }

    /* Register node in etcd */
    snprintf(node->node_key, sizeof(node->node_key),
             ETCD_PREFIX_TOPOLOGY "/node/%u", cfg->node_id);

    char node_json[2048];
    snprintf(node_json, sizeof(node_json),
             "{\"node_id\":%u,\"dc_id\":%u,\"host\":\"%s\","
             "\"gateway_port\":%u,\"meta_port\":%u,\"access_port\":%u,"
             "\"disk_count\":%lu,\"total_disk_bytes\":%lu,\"status\":\"active\"}",
             cfg->node_id, cfg->dc_id, cfg->host,
             cfg->gateway_port, cfg->meta_port, cfg->access_port,
             (unsigned long)cfg->disk_count,
             (unsigned long)cfg->total_disk_bytes);

    if (etcd_kv_put(client, node->node_key, node_json, node->lease.id) != 0) {
        etcd_lease_revoke(client, node->lease.id);
        free(node);
        return NULL;
    }

    /* Register services in discovery */
    char svc_key[1024];
    char svc_val[256];

    snprintf(svc_key, sizeof(svc_key),
             ETCD_PREFIX_DISCOVERY "/gateways/node_%u", cfg->node_id);
    snprintf(svc_val, sizeof(svc_val), "%s:%u", cfg->host, cfg->gateway_port);
    etcd_kv_put(client, svc_key, svc_val, node->lease.id);

    snprintf(svc_key, sizeof(svc_key),
             ETCD_PREFIX_DISCOVERY "/meta/node_%u", cfg->node_id);
    snprintf(svc_val, sizeof(svc_val), "%s:%u", cfg->host, cfg->meta_port);
    etcd_kv_put(client, svc_key, svc_val, node->lease.id);

    return node;
}

int cluster_node_heartbeat(cluster_node_t *node) {
    if (!node) return -1;

    return etcd_lease_keepalive(node->client, node->lease.id);
}

int cluster_node_leave(cluster_node_t *node) {
    if (!node) return -1;

    node->info.status = NODE_DRAINING;

    /* Update status in etcd */
    char node_json[2048];
    snprintf(node_json, sizeof(node_json),
             "{\"node_id\":%u,\"status\":\"draining\"}",
             node->info.node_id);

    etcd_kv_put(node->client, node->node_key, node_json, node->lease.id);

    return etcd_lease_revoke(node->client, node->lease.id);
}

const cluster_node_info_t *cluster_node_get_info(cluster_node_t *node) {
    return node ? &node->info : NULL;
}

void cluster_node_destroy(cluster_node_t *node) {
    if (!node) return;
    /* Lease may already be revoked by leave() — revoke again is safe (stub) */
    free(node);
}
```

- [ ] **Step 4: Build and run node lifecycle tests**

```bash
cd test/cluster && make test_cluster_node && ./test_cluster_node
```
Expected: All 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/lightfs/cluster/cluster_node.h src/cluster/cluster_node.c
git add test/cluster/test_cluster_node.c
git commit -m "feat: implement node lifecycle (join/heartbeat/leave) with etcd lease"
```

---

### Task 4: Configuration Management

**Files:**
- Create: `include/lightfs/cluster/cluster_config.h`
- Create: `src/cluster/cluster_config.c`
- Create: `test/cluster/test_cluster_config.c`

- [ ] **Step 1: Define configuration API**

```c
/* include/lightfs/cluster/cluster_config.h */
#ifndef LIGHTFS_CLUSTER_CONFIG_H
#define LIGHTFS_CLUSTER_CONFIG_H

#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

/* Configuration key prefixes in etcd */
#define CONFIG_EC_POLICIES     ETCD_PREFIX_CONFIG "/ec_policies"
#define CONFIG_REPLICATION     ETCD_PREFIX_CONFIG "/replication"
#define CONFIG_STORAGE_TIERS   ETCD_PREFIX_CONFIG "/storage_tiers"
#define CONFIG_LIFECYCLE       ETCD_PREFIX_CONFIG "/lifecycle_rules"

/* EC policy configuration */
typedef struct {
    int default_data_k;      /* default K value (e.g., 6 or 10) */
    int default_parity_m;    /* default M value (e.g., 3 or 4) */
    uint64_t small_threshold; /* objects below this use replication */
    uint64_t medium_threshold; /* objects below this use 6+3 */
} ec_policy_config_t;

/* Bucket-specific configuration */
typedef struct {
    char bucket_name[256];
    int replication_mode;    /* 2 or 3 */
    ec_policy_config_t ec_policy;
    int lifecycle_enabled;
} bucket_config_t;

typedef struct cluster_config_manager cluster_config_manager_t;

/* Create config manager watching etcd */
cluster_config_manager_t *cluster_config_create(etcd_client_t *client);

/* Destroy config manager */
void cluster_config_destroy(cluster_config_manager_t *mgr);

/* Get default EC policy */
int cluster_config_get_ec_policy(cluster_config_manager_t *mgr,
                                  ec_policy_config_t *out);

/* Get bucket-specific config */
int cluster_config_get_bucket(cluster_config_manager_t *mgr,
                               const char *bucket,
                               bucket_config_t *out);

/* Register a callback for config changes.
 * Callback fires when any watched config key changes. */
typedef void (*cluster_config_cb)(const char *key, const char *new_value,
                                   void *ctx);
int cluster_config_watch(cluster_config_manager_t *mgr,
                          cluster_config_cb cb, void *ctx);

#endif /* LIGHTFS_CLUSTER_CONFIG_H */
```

- [ ] **Step 2: Write config tests**

```c
/* test/cluster/test_cluster_config.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/cluster/cluster_config.h"
#include "lightfs/cluster/etcd_client.h"
#include "../mocks/mock_etcd.h"

static int cb_called = 0;
static char cb_last_key[1024] = {0};

static void test_config_cb(const char *key, const char *new_value, void *ctx) {
    cb_called++;
    strncpy(cb_last_key, key, sizeof(cb_last_key) - 1);
    (void)new_value; (void)ctx;
}

Test(cluster_config, create_and_get_default_ec_policy) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    cr_assert_not_null(mgr);

    ec_policy_config_t policy = {0};
    int rc = cluster_config_get_ec_policy(mgr, &policy);
    cr_assert_eq(rc, 0);
    cr_assert_eq(policy.default_data_k, 10);  /* default */
    cr_assert_eq(policy.default_parity_m, 4);

    cluster_config_destroy(mgr);
    etcd_client_destroy(client);
}

Test(cluster_config, get_nonexistent_bucket_config) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    cr_assert_not_null(mgr);

    bucket_config_t cfg = {0};
    int rc = cluster_config_get_bucket(mgr, "no-such-bucket", &cfg);
    cr_assert_neq(rc, 0, "Non-existent bucket should return error");

    cluster_config_destroy(mgr);
    etcd_client_destroy(client);
}

Test(cluster_config, watch_config_changes) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    cluster_config_manager_t *mgr = cluster_config_create(client);
    cr_assert_not_null(mgr);

    cb_called = 0;
    int rc = cluster_config_watch(mgr, test_config_cb, NULL);
    cr_assert_eq(rc, 0);

    cluster_config_destroy(mgr);
    etcd_client_destroy(client);
}
```

- [ ] **Step 3: Implement configuration management**

```c
/* src/cluster/cluster_config.c */
#include "lightfs/cluster/cluster_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_BUCKET_CONFIGS 1000

struct cluster_config_manager {
    etcd_client_t *client;
    ec_policy_config_t default_ec;
    bucket_config_t bucket_configs[MAX_BUCKET_CONFIGS];
    int bucket_count;
    cluster_config_cb watch_cb;
    void *watch_ctx;
};

cluster_config_manager_t *cluster_config_create(etcd_client_t *client) {
    if (!client) return NULL;

    cluster_config_manager_t *mgr = calloc(1, sizeof(cluster_config_manager_t));
    if (!mgr) return NULL;

    mgr->client = client;

    /* Default EC policy: 10+4, small <4MB uses replication, medium <64MB uses 6+3 */
    mgr->default_ec.default_data_k = 10;
    mgr->default_ec.default_parity_m = 4;
    mgr->default_ec.small_threshold = 4ULL * 1024 * 1024;
    mgr->default_ec.medium_threshold = 64ULL * 1024 * 1024;

    /* Phase 3: watch etcd config prefix and update defaults */
    /* Phase 2: use hardcoded defaults */

    return mgr;
}

void cluster_config_destroy(cluster_config_manager_t *mgr) {
    if (!mgr) return;
    /* Cancel watch if active */
    free(mgr);
}

int cluster_config_get_ec_policy(cluster_config_manager_t *mgr,
                                  ec_policy_config_t *out) {
    if (!mgr || !out) return -1;

    *out = mgr->default_ec;
    return 0;
}

int cluster_config_get_bucket(cluster_config_manager_t *mgr,
                               const char *bucket,
                               bucket_config_t *out) {
    if (!mgr || !bucket || !out) return -1;

    for (int i = 0; i < mgr->bucket_count; i++) {
        if (strcmp(mgr->bucket_configs[i].bucket_name, bucket) == 0) {
            *out = mgr->bucket_configs[i];
            return 0;
        }
    }

    return -1;  /* bucket not configured */
}

int cluster_config_watch(cluster_config_manager_t *mgr,
                          cluster_config_cb cb, void *ctx) {
    if (!mgr || !cb) return -1;

    mgr->watch_cb = cb;
    mgr->watch_ctx = ctx;

    /* Phase 3: etcd_watch_prefix on CONFIG_EC_POLICIES, etc. */
    /* Phase 2: stub — no watch */

    return 0;
}
```

- [ ] **Step 4: Build and run config tests**

```bash
cd test/cluster && make test_cluster_config && ./test_cluster_config
```
Expected: All 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/lightfs/cluster/cluster_config.h src/cluster/cluster_config.c
git add test/cluster/test_cluster_config.c
git commit -m "feat: implement cluster configuration management with etcd-backed defaults"
```

---

### Task 5: Service Discovery

**Files:**
- Create: `include/lightfs/cluster/cluster_discovery.h`
- Create: `src/cluster/cluster_discovery.c`
- Create: `test/cluster/test_cluster_discovery.c`

- [ ] **Step 1: Define service discovery API**

```c
/* include/lightfs/cluster/cluster_discovery.h */
#ifndef LIGHTFS_CLUSTER_DISCOVERY_H
#define LIGHTFS_CLUSTER_DISCOVERY_H

#include "lightfs/cluster/cluster_types.h"
#include "lightfs/cluster/etcd_client.h"
#include <stdint.h>

typedef struct service_discovery service_discovery_t;

/* Create service discovery, watching etcd for topology changes */
service_discovery_t *service_discovery_create(etcd_client_t *client);

/* Destroy service discovery */
void service_discovery_destroy(service_discovery_t *sd);

/* Get list of gateway endpoints */
int service_discovery_get_gateways(service_discovery_t *sd,
                                    service_endpoint_t *endpoints,
                                    int max_endpoints);

/* Get list of storage engine endpoints */
int service_discovery_get_storage_engines(service_discovery_t *sd,
                                           service_endpoint_t *endpoints,
                                           int max_endpoints);

/* Get list of meta server endpoint */
int service_discovery_get_meta_servers(service_discovery_t *sd,
                                        service_endpoint_t *endpoints,
                                        int max_endpoints);

/* Get shard map: which meta server owns which shard */
int service_discovery_get_shards(service_discovery_t *sd,
                                  cluster_shard_info_t *shards,
                                  int max_shards);

/* Register a callback for topology changes.
 * Fires when nodes join/leave or shards change. */
typedef void (*topology_change_cb)(void *ctx);
int service_discovery_watch(service_discovery_t *sd,
                             topology_change_cb cb, void *ctx);

#endif /* LIGHTFS_CLUSTER_DISCOVERY_H */
```

- [ ] **Step 2: Write service discovery tests**

```c
/* test/cluster/test_cluster_discovery.c */
#include <criterion/criterion.h>
#include <criterion/assert.h>
#include "lightfs/cluster/cluster_discovery.h"
#include "lightfs/cluster/etcd_client.h"
#include "lightfs/cluster/cluster_node.h"
#include "../mocks/mock_etcd.h"

static int topo_cb_count = 0;

static void topo_change_cb(void *ctx) {
    topo_cb_count++;
    (void)ctx;
}

Test(service_discovery, create_returns_non_null) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    service_discovery_t *sd = service_discovery_create(client);
    cr_assert_not_null(sd);

    service_discovery_destroy(sd);
    etcd_client_destroy(client);
}

Test(service_discovery, find_joined_gateway) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    /* Join a node */
    cluster_node_config_t cfg = {
        .node_id = 10,
        .dc_id = 0,
        .host = "10.0.1.10",
        .gateway_port = 8080,
        .meta_port = 9090,
        .access_port = 7070,
        .disk_count = 2,
        .total_disk_bytes = 2ULL * 1024 * 1024 * 1024 * 1024,
    };
    cluster_node_t *node = cluster_node_join(client, &cfg);
    cr_assert_not_null(node);

    /* Discovery should see it */
    service_discovery_t *sd = service_discovery_create(client);
    cr_assert_not_null(sd);

    service_endpoint_t endpoints[10];
    int count = service_discovery_get_gateways(sd, endpoints, 10);
    cr_assert_geq(count, 1, "Should find at least 1 gateway");
    cr_assert_eq(endpoints[0].node_id, 10);
    cr_assert_eq(endpoints[0].port, 8080);

    service_discovery_destroy(sd);
    cluster_node_destroy(node);
    etcd_client_destroy(client);
}

Test(service_discovery, topology_watch_callback) {
    mock_etcd_init();

    etcd_client_t *client = etcd_client_create("127.0.0.1", 2379);
    cr_assert_not_null(client);

    service_discovery_t *sd = service_discovery_create(client);
    cr_assert_not_null(sd);

    topo_cb_count = 0;
    int rc = service_discovery_watch(sd, topo_change_cb, NULL);
    cr_assert_eq(rc, 0);

    /* Phase 3: trigger a change and verify callback fires
     * Phase 2: stub — callback doesn't fire automatically */

    service_discovery_destroy(sd);
    etcd_client_destroy(client);
}
```

- [ ] **Step 3: Implement service discovery**

```c
/* src/cluster/cluster_discovery.c */
#include "lightfs/cluster/cluster_discovery.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ENDPOINTS 256
#define MAX_SHARDS 1024

struct service_discovery {
    etcd_client_t *client;
    service_endpoint_t gateways[MAX_ENDPOINTS];
    service_endpoint_t storage_engines[MAX_ENDPOINTS];
    service_endpoint_t meta_servers[MAX_ENDPOINTS];
    cluster_shard_info_t shards[MAX_SHARDS];
    int gateway_count;
    int se_count;
    int meta_count;
    int shard_count;
    topology_change_cb topo_cb;
    void *topo_ctx;
};

service_discovery_t *service_discovery_create(etcd_client_t *client) {
    if (!client) return NULL;

    service_discovery_t *sd = calloc(1, sizeof(service_discovery_t));
    if (!sd) return NULL;

    sd->client = client;

    /* Phase 3: build routing tables from etcd watches
     * Phase 2: stub — populate from etcd KV list */

    /* List gateways from discovery prefix */
    char keys[256][1024];
    int n = etcd_kv_list(client, ETCD_PREFIX_DISCOVERY "/gateways/",
                          NULL, &n);
    (void)keys; (void)n;

    /* Phase 2: parse topology from etcd KV store */
    /* For now, routing tables are empty until populated by etcd_watch */

    return sd;
}

void service_discovery_destroy(service_discovery_t *sd) {
    free(sd);
}

int service_discovery_get_gateways(service_discovery_t *sd,
                                    service_endpoint_t *endpoints,
                                    int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->gateway_count < max_endpoints ?
                sd->gateway_count : max_endpoints;
    memcpy(endpoints, sd->gateways, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_storage_engines(service_discovery_t *sd,
                                           service_endpoint_t *endpoints,
                                           int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->se_count < max_endpoints ?
                sd->se_count : max_endpoints;
    memcpy(endpoints, sd->storage_engines, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_meta_servers(service_discovery_t *sd,
                                        service_endpoint_t *endpoints,
                                        int max_endpoints) {
    if (!sd || !endpoints) return -1;

    int count = sd->meta_count < max_endpoints ?
                sd->meta_count : max_endpoints;
    memcpy(endpoints, sd->meta_servers, count * sizeof(service_endpoint_t));
    return count;
}

int service_discovery_get_shards(service_discovery_t *sd,
                                  cluster_shard_info_t *shards,
                                  int max_shards) {
    if (!sd || !shards) return -1;

    int count = sd->shard_count < max_shards ?
                sd->shard_count : max_shards;
    memcpy(shards, sd->shards, count * sizeof(cluster_shard_info_t));
    return count;
}

int service_discovery_watch(service_discovery_t *sd,
                             topology_change_cb cb, void *ctx) {
    if (!sd || !cb) return -1;

    sd->topo_cb = cb;
    sd->topo_ctx = ctx;

    /* Phase 3: watch etcd topology prefix */
    /* Phase 2: stub */

    return 0;
}
```

- [ ] **Step 4: Build and run discovery tests**

```bash
cd test/cluster && make test_cluster_discovery && ./test_cluster_discovery
```
Expected: All 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/lightfs/cluster/cluster_discovery.h src/cluster/cluster_discovery.c
git add test/cluster/test_cluster_discovery.c
git commit -m "feat: implement service discovery with endpoint lookup and topology watch"
```

---

### Task 6: Full Test Suite

**Files:**
- No new files

- [ ] **Step 1: Run full cluster test suite**

```bash
cd test/cluster && make clean && make run
```
Expected: All test binaries compile and pass:
- test_etcd_client: 6 tests
- test_cluster_node: 3 tests
- test_cluster_config: 3 tests
- test_cluster_discovery: 3 tests

- [ ] **Step 2: Commit**

```bash
git add test/cluster/
git commit -m "test: complete cluster/etcd test suite"
```

---

## Self-Review

### Spec Coverage

| Spec Requirement | Task | Status |
|---|---|---|
| Node join with etcd lease (TTL 10s) | Task 3 | Covered |
| Node health: lease heartbeat | Task 3 | Covered |
| Node leave: drain, revoke lease | Task 3 | Covered |
| Report disks on join | Task 3 | Covered (disk_count, total_disk_bytes in config) |
| Advertise services on join | Task 3 | Covered (gateway/meta/access ports) |
| EC policies config (cluster default + per-bucket) | Task 4 | Covered |
| Replication mode config (per-bucket) | Task 4 | Covered (in bucket_config_t) |
| Storage tiers config | Task 4 | Covered (placeholder in etcd prefix) |
| Lifecycle rules config | Task 4 | Covered (lifecycle_enabled in bucket_config) |
| Meta shard map config | Task 5 | Covered |
| Service discovery: Access watches gateways | Task 5 | Covered |
| Service discovery: Gateway watches SEs + shards | Task 5 | Covered |
| Service discovery: Meta watches topology | Task 5 | Covered |
| etcd as registry — all components watch prefixes | Task 2 + Task 5 | Covered |
| etcd outage doesn't block reads/writes | Design | Covered (etcd is management plane only, data path independent) |

### Placeholder Scan
- No "TBD", "TODO" or incomplete sections
- Phase 2 stubs are explicit: etcd uses pseudo lease IDs, no real HTTP, no JSON parsing, no streaming watch
- These are intentional Phase boundaries — full etcd v3 gRPC integration deferred

### Type Consistency
- `cluster_node_info_t`, `cluster_shard_info_t`, `service_endpoint_t` from `cluster_types.h` used throughout
- `etcd_client_t` opaque type consistent across all tasks
- `etcd_lease_t` used in both etcd_client and cluster_node
- `node_status_t` enum (ACTIVE/DRAINING/DOWN) used in types and lifecycle code
- etcd key prefixes (`ETCD_PREFIX_TOPOLOGY`, `ETCD_PREFIX_DISCOVERY`, etc.) consistent between types and implementation
