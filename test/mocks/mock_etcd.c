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

    for (int i = 0; i < g_store_count; i++) {
        if (g_store[i].exists && strcmp(g_store[i].key, key) == 0) {
            strncpy(g_store[i].value, value, sizeof(g_store[i].value) - 1);
            g_store[i].mod_revision = ++g_revision;
            g_store[i].lease_id = lease_id;
            return 0;
        }
    }

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
            value_out[value_cap - 1] = '\0';
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
            keys[count][1023] = '\0';
            count++;
        }
    }
    return count;
}

uint64_t mock_etcd_revision(void) {
    return g_revision;
}
