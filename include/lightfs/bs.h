#ifndef LIGHTFS_BS_H
#define LIGHTFS_BS_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int bs_init(const bs_config_t *config);
void bs_destroy(void);

typedef void (*bs_put_callback)(int result, const blob_location_t *location, void *user_data);
int bs_put_blob(blob_id_t id, const void *data, uint32_t size,
        bs_put_callback callback, void *user_data);

typedef void (*bs_get_callback)(int result, const void *data, uint32_t size, void *user_data);
int bs_get_blob(const blob_location_t *location, bs_get_callback callback, void *user_data);

typedef void (*bs_delete_callback)(int result, void *user_data);
int bs_delete_blob(blob_id_t id, bs_delete_callback callback, void *user_data);

int bs_stat_blob(blob_id_t id, blob_state_t *state_out);

int bs_garbage_collection_run(void);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTFS_BS_H */
