#ifndef LIGHTFS_META_RECOVERY_H
#define LIGHTFS_META_RECOVERY_H

#include "lightfs/meta/meta_server.h"

int meta_recovery_start(meta_server_t *server,
                         uint64_t checkpoint_blob_id,
                         uint64_t checkpoint_sequence);

int meta_recovery_background(meta_server_t *server,
                              uint32_t failed_node_id);

#endif /* LIGHTFS_META_RECOVERY_H */
