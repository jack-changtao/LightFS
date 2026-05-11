#ifndef LIGHTFS_COW_BTREE_INTERNAL_H
#define LIGHTFS_COW_BTREE_INTERNAL_H

#include "lightfs/bs_cow_btree.h"

#define BTREE_ORDER 16

typedef struct btree_node {
    btree_key_t keys[BTREE_ORDER - 1];
    blob_location_t values[BTREE_ORDER - 1];
    struct btree_node *children[BTREE_ORDER];
    int count;
    int is_leaf;
    uint64_t page_id;
} btree_node_t;

struct cow_btree {
    btree_node_t *root;
    uint64_t next_page_id;
    int is_dirty;
};

#endif /* LIGHTFS_COW_BTREE_INTERNAL_H */
