#include "meta_checkpoint.h"
#include "lightfs/meta/meta_types.h"
#include "lightfs/bs_cow_btree.h"
#include <stdlib.h>
#include <string.h>

#define CHECKPOINT_HEADER_MAGIC 0x4D434B50ULL

typedef struct checkpoint_header {
    uint64_t magic;
    uint64_t seq;
    uint64_t shard_id;
    uint32_t entry_count;
    uint32_t reserved;
} checkpoint_header_t;

static char g_checkpoint_buf[1024 * 1024];
static uint64_t g_checkpoint_id_counter = 0;

int meta_checkpoint_write(meta_shard_t *shard,
                           uint64_t seq,
                           uint64_t *checkpoint_blob_id_out) {
    if (!shard || !checkpoint_blob_id_out) return -1;

    checkpoint_header_t *hdr = (checkpoint_header_t *)g_checkpoint_buf;
    hdr->magic = CHECKPOINT_HEADER_MAGIC;
    hdr->seq = seq;
    hdr->shard_id = meta_shard_get_id(shard);
    hdr->entry_count = (uint32_t)meta_shard_get_count(shard);
    hdr->reserved = 0;

    char *p = g_checkpoint_buf + sizeof(checkpoint_header_t);
    int entry_count = meta_shard_get_count(shard);
    object_manifest_t *entries = meta_shard_get_entries(shard);

    for (int i = 0; i < entry_count; i++) {
        object_manifest_t *m = &entries[i];

        int blen = (int)strlen(m->bucket) + 1;
        memcpy(p, m->bucket, blen);
        p += blen;

        int klen = (int)strlen(m->key) + 1;
        memcpy(p, m->key, klen);
        p += klen;

        memcpy(p, &m->size, sizeof(m->size));
        p += sizeof(m->size);
        memcpy(p, &m->crc, sizeof(m->crc));
        p += sizeof(m->crc);
        memcpy(p, &m->write_seq, sizeof(m->write_seq));
        p += sizeof(m->write_seq);
        memcpy(p, &m->dc_id, sizeof(m->dc_id));
        p += sizeof(m->dc_id);
    }

    int total_len = (int)(p - g_checkpoint_buf);
    g_checkpoint_id_counter++;
    *checkpoint_blob_id_out = g_checkpoint_id_counter;

    return total_len;
}

int meta_checkpoint_read(meta_shard_t *shard,
                          uint64_t checkpoint_blob_id) {
    (void)checkpoint_blob_id;

    if (!shard) return -1;

    checkpoint_header_t *hdr = (checkpoint_header_t *)g_checkpoint_buf;
    if (hdr->magic != CHECKPOINT_HEADER_MAGIC) return -1;

    char *p = g_checkpoint_buf + sizeof(checkpoint_header_t);

    for (uint32_t i = 0; i < hdr->entry_count; i++) {
        object_manifest_t m;
        memset(&m, 0, sizeof(m));

        int blen = (int)strlen(p) + 1;
        strncpy(m.bucket, p, sizeof(m.bucket) - 1);
        p += blen;

        int klen = (int)strlen(p) + 1;
        strncpy(m.key, p, sizeof(m.key) - 1);
        p += klen;

        memcpy(&m.size, p, sizeof(m.size));
        p += sizeof(m.size);
        memcpy(&m.crc, p, sizeof(m.crc));
        p += sizeof(m.crc);
        memcpy(&m.write_seq, p, sizeof(m.write_seq));
        p += sizeof(m.write_seq);
        memcpy(&m.dc_id, p, sizeof(m.dc_id));
        p += sizeof(m.dc_id);

        meta_shard_insert(shard, &m);
    }

    return 0;
}
