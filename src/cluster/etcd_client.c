#include "lightfs/cluster/etcd_client.h"
#include "../test/mocks/mock_etcd.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct etcd_client {
    char host[256];
    uint16_t port;
    int watch_running;
};

etcd_client_t *etcd_client_create(const char *host, uint16_t port) {
    etcd_client_t *c = calloc(1, sizeof(etcd_client_t));
    if (!c) return NULL;

    strncpy(c->host, host ? host : ETCD_DEFAULT_HOST, sizeof(c->host) - 1);
    c->port = port ? port : ETCD_DEFAULT_PORT;

    return c;
}

void etcd_client_destroy(etcd_client_t *client) {
    free(client);
}

int etcd_lease_grant(etcd_client_t *client, uint64_t ttl, etcd_lease_t *lease) {
    if (!client || !lease) return -1;

    lease->id = mock_etcd_lease_grant(ttl);
    lease->ttl = ttl;
    return 0;
}

int etcd_lease_keepalive(etcd_client_t *client, uint64_t lease_id) {
    if (!client) return -1;
    (void)lease_id;
    return 0;
}

int etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id) {
    if (!client) return -1;
    mock_etcd_lease_revoke(lease_id);
    return 0;
}

int etcd_kv_put(etcd_client_t *client, const char *key, const char *value,
                 uint64_t lease_id) {
    if (!client || !key || !value) return -1;

    return mock_etcd_put(key, value, lease_id);
}

int etcd_kv_get(etcd_client_t *client, const char *key,
                 etcd_kv_response_t *resp) {
    if (!client || !key || !resp) return -1;

    char value[4096];
    int rc = mock_etcd_get(key, value, sizeof(value), NULL, NULL);
    if (rc == 0) {
        resp->key = strdup(key);
        resp->value = strdup(value);
        return 0;
    }
    resp->key = NULL;
    resp->value = NULL;
    return -1;
}

int etcd_kv_delete(etcd_client_t *client, const char *key) {
    if (!client || !key) return -1;
    return mock_etcd_delete(key);
}

int etcd_watch_prefix(etcd_client_t *client, const char *prefix,
                       etcd_watch_cb cb, void *ctx) {
    if (!client || !prefix || !cb) return -1;
    (void)cb;
    (void)ctx;
    return 0;
}

int etcd_watch_cancel(etcd_client_t *client) {
    if (!client) return -1;
    client->watch_running = 0;
    return 0;
}

int etcd_kv_list(etcd_client_t *client, const char *prefix,
                  etcd_kv_response_t **results, int *count) {
    if (!client || !prefix || !results || !count) return -1;

    char keys[256][1024];
    int n = mock_etcd_list(prefix, keys, 256);

    if (n <= 0) {
        *results = NULL;
        *count = 0;
        return 0;
    }

    *results = calloc(n, sizeof(etcd_kv_response_t));
    if (!*results) return -1;

    *count = n;
    for (int i = 0; i < n; i++) {
        char value[4096];
        if (mock_etcd_get(keys[i], value, sizeof(value), NULL, NULL) == 0) {
            (*results)[i].key = strdup(keys[i]);
            (*results)[i].value = strdup(value);
        }
    }
    return 0;
}
