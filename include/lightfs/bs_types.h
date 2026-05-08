#ifndef LIGHTFS_BS_TYPES_H
#define LIGHTFS_BS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t blob_id_t;
typedef uint64_t segment_id_t;
typedef uint64_t btree_key_t;

#define BLOB_ID_INVALID    ((blob_id_t)0)
#define SEGMENT_ID_INVALID ((segment_id_t)0)

typedef struct {
    segment_id_t segment_id;
    uint64_t offset;
    uint32_t size;
    uint32_t crc;
} blob_location_t;

typedef enum {
    BLOB_STATE_FREE = 0,
    BLOB_STATE_ACTIVE,
    BLOB_STATE_DELETED,
} blob_state_t;

#endif /* LIGHTFS_BS_TYPES_H */
