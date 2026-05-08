#ifndef LIGHTFS_BS_CONFIG_H
#define LIGHTFS_BS_CONFIG_H

#include <stdint.h>

#define BS_DEFAULT_SEGMENT_SIZE   (256ULL * 1024 * 1024)  /* 256 MB */
#define BS_DEFAULT_JOURNAL_SIZE   (64ULL * 1024 * 1024)   /* 64 MB */
#define BS_DEFAULT_META_SIZE      (64ULL * 1024 * 1024)    /* 64 MB */
#define BS_MAX_BLOB_SIZE          (4ULL * 1024 * 1024)       /* 4 MB */
#define BS_GC_LIVENESS_THRESHOLD 20  /* percent */
#define BS_SUPERBLOCK_MAGIC       0x4C465331ULL  /* "LFS1" */
#define BS_SUPERBLOCK_OFFSET      0

typedef struct bs_config {
    const char *bdev_name;
    uint64_t segment_size;
    uint64_t journal_size;
    uint64_t meta_size;
    uint32_t gc_liveness_threshold;
    int read_only;
} bs_config_t;

#endif /* LIGHTFS_BS_CONFIG_H */
