#include "meta_bucket_registry.h"
#include <stdlib.h>
#include <string.h>

#define REGISTRY_MAXIMUM 10000

struct bucket_registry {
  bucket_entry_t entries[REGISTRY_MAXIMUM];
  int count;
};

bucket_registry_t *bucket_registry_create(void) {
  return calloc(1, sizeof(bucket_registry_t));
}

void bucket_registry_destroy(bucket_registry_t *registry) {
  free(registry);
}

int bucket_registry_add(bucket_registry_t *registry, const bucket_entry_t *entry) {
  if (!registry || !entry || registry->count >= REGISTRY_MAXIMUM) return -1;

  for (int i = 0; i < registry->count; i++) {
    if (strcmp(registry->entries[i].name, entry->name) == 0) {
      return -1;
    }
  }

  registry->entries[registry->count++] = *entry;
  return 0;
}

int bucket_registry_lookup(bucket_registry_t *registry, const char *bucket,
              bucket_entry_t *entry_out) {
  if (!registry || !bucket || !entry_out) return -1;

  for (int i = 0; i < registry->count; i++) {
    if (strcmp(registry->entries[i].name, bucket) == 0) {
      *entry_out = registry->entries[i];
      return 0;
    }
  }
  return -1;
}
