#ifndef LIGHTFS_BS_H
#define LIGHTFS_BS_H

#include "lightfs/bs_types.h"
#include "lightfs/bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int bs_init(const bs_config_t *cfg);
void bs_destroy(void);

typedef void (*bs_put_cb)(int rc, const blob_location_t *loc, void *arg);
int bs_put_blob(blob_id_t id, const void *data, uint32_t size,
                bs_put_cb cb, void *arg);

typedef void (*bs_get_cb)(int rc, const void *data, uint32_t size, void *arg);
int bs_get_blob(const blob_location_t *loc, bs_get_cb cb, void *arg);

typedef void (*bs_delete_cb)(int rc, void *arg);
int bs_delete_blob(blob_id_t id, bs_delete_cb cb, void *arg);

int bs_stat_blob(blob_id_t id, blob_state_t *state_out);

int bs_gc_run(void);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTFS_BS_H */
