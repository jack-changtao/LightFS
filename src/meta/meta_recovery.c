#include "lightfs/meta/meta_recovery.h"
#include "meta_checkpoint.h"
#include <stdio.h>

int meta_recovery_start(meta_server_t *server,
            uint64_t checkpoint_blob_id,
            uint64_t checkpoint_sequence) {
  if (!server) return -1;

  printf("Meta Server %u: starting recovery from checkpoint seq=%lu\n",
     meta_server_get_id(server), (unsigned long)checkpoint_sequence);

  if (checkpoint_blob_id > 0) {
    (void)checkpoint_blob_id;
  }

  printf("Meta Server %u: recovery complete\n", meta_server_get_id(server));
  return 0;
}

int meta_recovery_background(meta_server_t *server,
               uint32_t failed_node_id) {
  if (!server) return -1;

  printf("Meta Server %u: background recovery for failed node %u\n",
     meta_server_get_id(server), failed_node_id);

  (void)failed_node_id;

  return 0;
}
