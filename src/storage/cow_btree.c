#include "cow_btree.h"
#include <stdlib.h>
#include <string.h>

cow_btree_t *cow_btree_create(void) {
    cow_btree_t *tree = calloc(1, sizeof(cow_btree_t));
    if (!tree) return NULL;

    tree->root = calloc(1, sizeof(btree_node_t));
    if (!tree->root) {
        free(tree);
        return NULL;
    }
    tree->root->is_leaf = 1;
    tree->next_page_id = 1;
    return tree;
}

void cow_btree_destroy(cow_btree_t *tree) {
    if (!tree) return;
    free(tree->root);
    free(tree);
}

int cow_btree_insert(cow_btree_t *tree, btree_key_t key,
                     const blob_location_t *value) {
    if (!tree || !tree->root || !value) return -1;

    btree_node_t *node = tree->root;
    while (!node->is_leaf) {
        int i = 0;
        while (i < node->count && key > node->keys[i]) i++;
        node = node->children[i];
        if (!node) return -1;
    }

    int i = node->count - 1;
    while (i >= 0 && node->keys[i] > key) {
        if (node->keys[i] == key) {
            node->values[i] = *value;
            tree->is_dirty = 1;
            return 0;
        }
        node->keys[i + 1] = node->keys[i];
        node->values[i + 1] = node->values[i];
        i--;
    }
    node->keys[i + 1] = key;
    node->values[i + 1] = *value;
    node->count++;
    tree->is_dirty = 1;
    return 0;
}

int cow_btree_lookup(cow_btree_t *tree, btree_key_t key,
                     blob_location_t *value_out) {
    if (!tree || !tree->root || !value_out) return -1;

    btree_node_t *node = tree->root;
    while (!node->is_leaf) {
        int i = 0;
        while (i < node->count && key > node->keys[i]) i++;
        node = node->children[i];
        if (!node) return -1;
    }

    for (int i = 0; i < node->count; i++) {
        if (node->keys[i] == key) {
            *value_out = node->values[i];
            return 0;
        }
    }
    return -1;
}

int cow_btree_delete(cow_btree_t *tree, btree_key_t key) {
    if (!tree || !tree->root) return -1;

    btree_node_t *node = tree->root;
    while (!node->is_leaf) {
        int i = 0;
        while (i < node->count && key > node->keys[i]) i++;
        node = node->children[i];
        if (!node) return -1;
    }

    for (int i = 0; i < node->count; i++) {
        if (node->keys[i] == key) {
            for (int j = i; j < node->count - 1; j++) {
                node->keys[j] = node->keys[j + 1];
                node->values[j] = node->values[j + 1];
            }
            node->count--;
            tree->is_dirty = 1;
            return 0;
        }
    }
    return -1;
}

int cow_btree_serialize(cow_btree_t *tree, void *buffer, int buffer_size) {
    if (!tree || !buffer || buffer_size < (int)sizeof(uint32_t)) return -1;

    uint8_t *cursor = buffer;
    uint32_t node_count = 1;

    memcpy(cursor, &node_count, sizeof(node_count));
    cursor += sizeof(node_count);

    memcpy(cursor, &tree->root->count, sizeof(tree->root->count));
    cursor += sizeof(tree->root->count);

    memcpy(cursor, tree->root->keys, tree->root->count * sizeof(btree_key_t));
    cursor += tree->root->count * sizeof(btree_key_t);

    memcpy(cursor, tree->root->values, tree->root->count * sizeof(blob_location_t));
    cursor += tree->root->count * sizeof(blob_location_t);

    return (int)(cursor - (uint8_t *)buffer);
}

int cow_btree_deserialize(cow_btree_t *tree, const void *buffer, int buffer_size) {
    if (!tree || !buffer || buffer_size < (int)sizeof(uint32_t)) return -1;

    const uint8_t *cursor = buffer;
    uint32_t node_count;
    memcpy(&node_count, cursor, sizeof(node_count));
    cursor += sizeof(node_count);

    memcpy(&tree->root->count, cursor, sizeof(tree->root->count));
    cursor += sizeof(tree->root->count);

    memcpy(tree->root->keys, cursor, tree->root->count * sizeof(btree_key_t));
    cursor += tree->root->count * sizeof(btree_key_t);

    memcpy(tree->root->values, cursor, tree->root->count * sizeof(blob_location_t));
    cursor += tree->root->count * sizeof(blob_location_t);

    tree->root->is_leaf = 1;
    return 0;
}
