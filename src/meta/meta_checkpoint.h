#ifndef LIGHTFS_META_CHECKPOINT_H
#define LIGHTFS_META_CHECKPOINT_H

#include "lightfs/meta/meta_shard.h"
#include "lightfs/bs_types.h"

int meta_checkpoint_write(meta_shard_t *shard,
                           uint64_t sequence,
                           uint64_t *checkpoint_blob_id_out);

int meta_checkpoint_read(meta_shard_t *shard,
                          uint64_t checkpoint_blob_id);

#endif /* LIGHTFS_META_CHECKPOINT_H */
