#ifndef LIGHTFS_GATEWAY_TYPES_H
#define LIGHTFS_GATEWAY_TYPES_H

#include "lightfs/bs_types.h"
#include "lightfs/meta/meta_types.h"
#include "lightfs/cluster/cluster_types.h"
#include <stdint.h>

#define GATEWAY_MAX_FRAGMENTS 20
#define GATEWAY_MAX_STRIPE_SIZE (4 * 1024 * 1024)
#define GATEWAY_MEDIUM_STRIPE_SIZE (512 * 1024)
#define GATEWAY_SMALL_THRESHOLD (4ULL * 1024 * 1024)
#define GATEWAY_MEDIUM_THRESHOLD (64ULL * 1024 * 1024)
#define GATEWAY_MANIFEST_CACHE_SIZE 10000

typedef enum {
    EC_REPLICATION_2X = 0,
    EC_REPLICATION_3X,
    EC_6_PLUS_3,
    EC_10_PLUS_4,
} ec_policy_t;

typedef struct {
    uint32_t fragment_index;
    uint32_t node_id;
    uint32_t disk_id;
    blob_location_t location;
    uint8_t *data;
    uint32_t size;
} fragment_t;

typedef enum {
    DOMAIN_DC = 0,
    DOMAIN_RACK,
    DOMAIN_HOST,
    DOMAIN_DISK,
} placement_domain_t;

typedef struct {
    uint32_t node_id;
    uint32_t disk_id;
    uint32_t dc_id;
    uint32_t rack_id;
    uint32_t host_id;
    uint64_t free_bytes;
} placement_target_t;

typedef struct {
    char bucket[META_MAX_BUCKET_LEN + 1];
    char key[META_MAX_KEY_LEN + 1];
    const uint8_t *data;
    uint64_t size;
    ec_policy_t ec_policy_override;
    int ec_override;
} gateway_put_request_t;

typedef struct {
    uint8_t *data;
    uint64_t size;
    int rc;
} gateway_get_response_t;

typedef struct {
    uint32_t node_id;
    uint32_t dc_id;
    uint16_t gateway_port;
    ec_policy_t default_ec_policy;
    int default_replication_factor;
    uint32_t manifest_cache_size;
} gateway_config_t;

#endif /* LIGHTFS_GATEWAY_TYPES_H */
