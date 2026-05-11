#include "meta_checkpoint.h"
#include "lightfs/meta/meta_types.h"
#include "lightfs/bs_cow_btree.h"
#include <stdlib.h>
#include <string.h>

#define CHECKPOINT_HEADER_MAGIC 0x4D434B50ULL

typedef struct checkpoint_header {
    uint64_t magic;
    uint64_t sequence;
    uint64_t shard_id;
    uint32_t entry_count;
    uint32_t reserved;
} checkpoint_header_t;

static char g_checkpoint_buffer[1024 * 1024];
static uint64_t g_checkpoint_id_counter = 0;

int meta_checkpoint_write(meta_shard_t *shard,
                           uint64_t sequence,
                           uint64_t *checkpoint_blob_id_out) {
    if (!shard || !checkpoint_blob_id_out) return -1;

    checkpoint_header_t *header = (checkpoint_header_t *)g_checkpoint_buffer;
    header->magic = CHECKPOINT_HEADER_MAGIC;
    header->sequence = sequence;
    header->shard_id = meta_shard_get_id(shard);
    header->entry_count = (uint32_t)meta_shard_get_count(shard);
    header->reserved = 0;

    char *cursor = g_checkpoint_buffer + sizeof(checkpoint_header_t);
    int entry_count = meta_shard_get_count(shard);
    object_manifest_t *entries = meta_shard_get_entries(shard);

    for (int i = 0; i < entry_count; i++) {
        object_manifest_t *manifest = &entries[i];

        int bucket_name_length = (int)strlen(manifest->bucket) + 1;
        memcpy(cursor, manifest->bucket, bucket_name_length);
        cursor += bucket_name_length;

        int key_length = (int)strlen(manifest->key) + 1;
        memcpy(cursor, manifest->key, key_length);
        cursor += key_length;

        memcpy(cursor, &manifest->size, sizeof(manifest->size));
        cursor += sizeof(manifest->size);
        memcpy(cursor, &manifest->checksum, sizeof(manifest->checksum));
        cursor += sizeof(manifest->checksum);
        memcpy(cursor, &manifest->write_sequence, sizeof(manifest->write_sequence));
        cursor += sizeof(manifest->write_sequence);
        memcpy(cursor, &manifest->datacenter_id, sizeof(manifest->datacenter_id));
        cursor += sizeof(manifest->datacenter_id);
    }

    int total_length = (int)(cursor - g_checkpoint_buffer);
    g_checkpoint_id_counter++;
    *checkpoint_blob_id_out = g_checkpoint_id_counter;

    return total_length;
}

int meta_checkpoint_read(meta_shard_t *shard,
                          uint64_t checkpoint_blob_id) {
    (void)checkpoint_blob_id;

    if (!shard) return -1;

    checkpoint_header_t *header = (checkpoint_header_t *)g_checkpoint_buffer;
    if (header->magic != CHECKPOINT_HEADER_MAGIC) return -1;

    char *cursor = g_checkpoint_buffer + sizeof(checkpoint_header_t);

    for (uint32_t i = 0; i < header->entry_count; i++) {
        object_manifest_t manifest;
        memset(&manifest, 0, sizeof(manifest));

        int bucket_name_length = (int)strlen(cursor) + 1;
        strncpy(manifest.bucket, cursor, sizeof(manifest.bucket) - 1);
        cursor += bucket_name_length;

        int key_length = (int)strlen(cursor) + 1;
        strncpy(manifest.key, cursor, sizeof(manifest.key) - 1);
        cursor += key_length;

        memcpy(&manifest.size, cursor, sizeof(manifest.size));
        cursor += sizeof(manifest.size);
        memcpy(&manifest.checksum, cursor, sizeof(manifest.checksum));
        cursor += sizeof(manifest.checksum);
        memcpy(&manifest.write_sequence, cursor, sizeof(manifest.write_sequence));
        cursor += sizeof(manifest.write_sequence);
        memcpy(&manifest.datacenter_id, cursor, sizeof(manifest.datacenter_id));
        cursor += sizeof(manifest.datacenter_id);

        meta_shard_insert(shard, &manifest);
    }

    return 0;
}
