#ifndef LIGHTFS_META_TYPES_H
#define LIGHTFS_META_TYPES_H

#include "lightfs/bs_types.h"
#include <stdint.h>
#include <stdbool.h>

#define META_MAX_BUCKET_LENGTH 255
#define META_MAX_KEY_LENGTH    1024

typedef struct object_manifest {
  char bucket[META_MAX_BUCKET_LENGTH + 1];
  char key[META_MAX_KEY_LENGTH + 1];
  uint64_t size;
  uint32_t checksum;
  uint64_t write_sequence;
  uint32_t datacenter_id;
  uint32_t fragment_count;
  blob_location_t *fragments;
} object_manifest_t;

typedef struct manifest_batch {
  object_manifest_t *manifests;
  int count;
  int capacity;
} manifest_batch_t;

typedef enum {
  SHARD_ACTIVE = 0,
  SHARD_SPLITTING,
  SHARD_LOADING,
} shard_status_t;

typedef struct meta_shard_info {
  uint32_t shard_id;
  uint32_t owner_meta_server_id;
  shard_status_t status;
  char key_min[META_MAX_KEY_LENGTH + 1];
  char key_max[META_MAX_KEY_LENGTH + 1];
  uint32_t parent_shard_id;
  uint64_t checkpoint_sequence;
} meta_shard_info_t;

typedef struct bucket_entry {
  char name[META_MAX_BUCKET_LENGTH + 1];
  uint32_t shard_id;
  int replication_mode;
  int erasure_coding_policy;
} bucket_entry_t;

#endif /* LIGHTFS_META_TYPES_H */
