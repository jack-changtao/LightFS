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

etcd_client_t *etcd_client_create(const char *host, uint16_t port);
void etcd_client_destroy(etcd_client_t *client);

int etcd_lease_grant(etcd_client_t *client, uint64_t ttl, etcd_lease_t *lease);
int etcd_lease_keepalive(etcd_client_t *client, uint64_t lease_id);
int etcd_lease_revoke(etcd_client_t *client, uint64_t lease_id);

int etcd_kv_put(etcd_client_t *client, const char *key, const char *value,
                 uint64_t lease_id);
int etcd_kv_get(etcd_client_t *client, const char *key,
                 etcd_kv_response_t *resp);
int etcd_kv_delete(etcd_client_t *client, const char *key);

typedef void (*etcd_watch_cb)(const char *key, const char *value,
                               bool deleted, void *ctx);
int etcd_watch_prefix(etcd_client_t *client, const char *prefix,
                       etcd_watch_cb cb, void *ctx);
int etcd_watch_cancel(etcd_client_t *client);

int etcd_kv_list(etcd_client_t *client, const char *prefix,
                  etcd_kv_response_t **results, int *count);

#endif /* LIGHTFS_ETCD_CLIENT_H */
