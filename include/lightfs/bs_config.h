#ifndef LIGHTFS_BS_CONFIG_H
#define LIGHTFS_BS_CONFIG_H

#include <stdint.h>

#define BS_DEFAULT_SEGMENT_SIZE   (256ULL * 1024 * 1024)  /* 256 MB */
#define BS_DEFAULT_JOURNAL_SIZE   (64ULL * 1024 * 1024)   /* 64 MB */
#define BS_DEFAULT_METADATA_SIZE  (64ULL * 1024 * 1024)   /* 64 MB */
#define BS_MAX_BLOB_SIZE          (4ULL * 1024 * 1024)    /* 4 MB */
#define BS_GARBAGE_COLLECTION_LIVENESS_THRESHOLD 20  /* percent */
#define BS_SUPERBLOCK_MAGIC       0x4C465331ULL  /* "LFS1" */
#define BS_SUPERBLOCK_OFFSET      0

typedef struct bs_config {
    const char *block_device_name;
    uint64_t segment_size;
    uint64_t journal_size;
    uint64_t metadata_size;
    uint32_t garbage_collection_liveness_threshold;
    int is_read_only;
} bs_config_t;

#endif /* LIGHTFS_BS_CONFIG_H */
