#ifndef LIGHTFS_BS_COW_BTREE_H
#define LIGHTFS_BS_COW_BTREE_H

#include "lightfs/bs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cow_btree cow_btree_t;

cow_btree_t *cow_btree_create(void);
void cow_btree_destroy(cow_btree_t *tree);

int cow_btree_insert(cow_btree_t *tree, btree_key_t key,
                     const blob_location_t *value);

int cow_btree_lookup(cow_btree_t *tree, btree_key_t key,
                     blob_location_t *value_out);

int cow_btree_delete(cow_btree_t *tree, btree_key_t key);

int cow_btree_serialize(cow_btree_t *tree, void *buffer, int buffer_size);

int cow_btree_deserialize(cow_btree_t *tree, const void *buffer, int buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTFS_BS_COW_BTREE_H */
