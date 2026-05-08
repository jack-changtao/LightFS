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

void mock_etcd_init(void);
uint64_t mock_etcd_lease_grant(uint64_t ttl);
void mock_etcd_lease_revoke(uint64_t lease_id);
int mock_etcd_put(const char *key, const char *value, uint64_t lease_id);
int mock_etcd_get(const char *key, char *value_out, int value_cap,
                   uint64_t *mod_rev, uint64_t *create_rev);
int mock_etcd_delete(const char *key);
int mock_etcd_list(const char *prefix, char keys[][1024], int max_keys);
uint64_t mock_etcd_revision(void);

#endif /* LIGHTFS_MOCK_ETCD_H */
