#include "lightfs/meta/meta_server.h"
#include "lightfs/meta/meta_shard.h"
#include "meta_bucket_registry.h"
#include "lightfs/bs_cow_btree.h"
#include <stdlib.h>
#include <string.h>

struct meta_server {
    meta_server_config_t config;
    uint64_t write_seq_counter;
    meta_shard_t *shard;
    bucket_registry_t *registry;
};

meta_server_t *meta_server_create(const meta_server_config_t *cfg) {
    if (!cfg) return NULL;

    meta_server_t *ms = calloc(1, sizeof(meta_server_t));
    if (!ms) return NULL;

    ms->config = *cfg;
    ms->write_seq_counter = 0;

    ms->shard = meta_shard_create(cfg->server_id, 0, "");
    if (!ms->shard) {
        free(ms);
        return NULL;
    }

    ms->registry = bucket_registry_create();
    if (!ms->registry) {
        meta_shard_destroy(ms->shard);
        free(ms);
        return NULL;
    }

    return ms;
}

void meta_server_destroy(meta_server_t *ms) {
    if (!ms) return;
    if (ms->shard) meta_shard_destroy(ms->shard);
    if (ms->registry) bucket_registry_destroy(ms->registry);
    free(ms);
}

int meta_server_push_manifest_batch(meta_server_t *ms,
                                     const manifest_batch_t *batch) {
    if (!ms || !batch || !batch->manifests || batch->count <= 0) return -1;

    for (int i = 0; i < batch->count; i++) {
        object_manifest_t *m = &batch->manifests[i];

        m->write_seq = ++ms->write_seq_counter;
        m->dc_id = ms->config.dc_id;

        int rc = meta_shard_insert(ms->shard, m);
        if (rc != 0) return -1;
    }

    return 0;
}

int meta_server_get_manifest(meta_server_t *ms,
                              const char *bucket, const char *key,
                              object_manifest_t *manifest_out) {
    if (!ms || !bucket || !key || !manifest_out) return -1;

    return meta_shard_lookup(ms->shard, bucket, key, manifest_out);
}

int meta_server_list_objects(meta_server_t *ms,
                              const char *bucket,
                              const char *prefix,
                              const char *marker,
                              int max_keys,
                              char **keys_out,
                              int *count_out) {
    if (!ms || !bucket || !keys_out || !count_out) return -1;

    return meta_shard_list(ms->shard, bucket, prefix, marker,
                           max_keys, keys_out, count_out);
}

int meta_server_checkpoint(meta_server_t *ms, uint32_t shard_id) {
    if (!ms) return -1;
    (void)shard_id;
    return 0;
}

uint32_t meta_server_get_id(meta_server_t *ms) {
    return ms ? ms->config.server_id : 0;
}
