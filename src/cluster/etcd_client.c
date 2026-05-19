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
  etcd_client_t *client = calloc(1, sizeof(etcd_client_t));
  if (!client) return NULL;

  strncpy(client->host, host ? host : ETCD_DEFAULT_HOST, sizeof(client->host) - 1);
  client->port = port ? port : ETCD_DEFAULT_PORT;

  return client;
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

int etcd_key_value_put(etcd_client_t *client, const char *key, const char *value,
        uint64_t lease_id) {
  if (!client || !key || !value) return -1;

  return mock_etcd_put(key, value, lease_id);
}

int etcd_key_value_get(etcd_client_t *client, const char *key,
        etcd_key_value_response_t *response) {
  if (!client || !key || !response) return -1;

  char value[4096];
  int result = mock_etcd_get(key, value, sizeof(value), NULL, NULL);
  if (result == 0) {
    response->key = strdup(key);
    response->value = strdup(value);
    return 0;
  }
  response->key = NULL;
  response->value = NULL;
  return -1;
}

int etcd_key_value_delete(etcd_client_t *client, const char *key) {
  if (!client || !key) return -1;
  return mock_etcd_delete(key);
}

int etcd_watch_prefix(etcd_client_t *client, const char *prefix,
           etcd_watch_callback callback, void *context) {
  if (!client || !prefix || !callback) return -1;
  (void)callback;
  (void)context;
  return 0;
}

int etcd_watch_cancel(etcd_client_t *client) {
  if (!client) return -1;
  client->watch_running = 0;
  return 0;
}

int etcd_key_value_list(etcd_client_t *client, const char *prefix,
         etcd_key_value_response_t **results, int *count) {
  if (!client || !prefix || !results || !count) return -1;

  char keys[256][1024];
  int key_count = mock_etcd_list(prefix, keys, 256);

  if (key_count <= 0) {
    *results = NULL;
    *count = 0;
    return 0;
  }

  *results = calloc(key_count, sizeof(etcd_key_value_response_t));
  if (!*results) return -1;

  *count = key_count;
  for (int i = 0; i < key_count; i++) {
    char value[4096];
    if (mock_etcd_get(keys[i], value, sizeof(value), NULL, NULL) == 0) {
      (*results)[i].key = strdup(keys[i]);
      (*results)[i].value = strdup(value);
    }
  }
  return 0;
}
