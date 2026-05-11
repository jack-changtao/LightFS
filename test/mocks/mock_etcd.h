#ifndef LIGHTFS_MOCK_ETCD_H
#define LIGHTFS_MOCK_ETCD_H

#include <stdint.h>
#include <stdbool.h>

#define MOCK_ETCD_MAX_KEYS 256

typedef struct mock_etcd_key_value {
    char key[1024];
    char value[4096];
    uint64_t lease_id;
    uint64_t modified_revision;
    uint64_t create_revision;
    bool is_present;
} mock_etcd_key_value_t;

void mock_etcd_init(void);
uint64_t mock_etcd_lease_grant(uint64_t time_to_live);
void mock_etcd_lease_revoke(uint64_t lease_id);
int mock_etcd_put(const char *key, const char *value, uint64_t lease_id);
int mock_etcd_get(const char *key, char *value_out, int value_capacity,
                   uint64_t *modified_revision, uint64_t *create_revision);
int mock_etcd_delete(const char *key);
int mock_etcd_list(const char *prefix, char keys[][1024], int max_keys);
uint64_t mock_etcd_revision(void);

#endif /* LIGHTFS_MOCK_ETCD_H */
