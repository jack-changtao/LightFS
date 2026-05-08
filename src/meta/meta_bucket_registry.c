#include "meta_bucket_registry.h"
#include <stdlib.h>
#include <string.h>

#define REGISTRY_MAX 10000

struct bucket_registry {
    bucket_entry_t entries[REGISTRY_MAX];
    int count;
};

bucket_registry_t *bucket_registry_create(void) {
    return calloc(1, sizeof(bucket_registry_t));
}

void bucket_registry_destroy(bucket_registry_t *reg) {
    free(reg);
}

int bucket_registry_add(bucket_registry_t *reg, const bucket_entry_t *entry) {
    if (!reg || !entry || reg->count >= REGISTRY_MAX) return -1;

    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, entry->name) == 0) {
            return -1;
        }
    }

    reg->entries[reg->count++] = *entry;
    return 0;
}

int bucket_registry_lookup(bucket_registry_t *reg, const char *bucket,
                            bucket_entry_t *out) {
    if (!reg || !bucket || !out) return -1;

    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, bucket) == 0) {
            *out = reg->entries[i];
            return 0;
        }
    }
    return -1;
}
