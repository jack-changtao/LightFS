#ifndef LIGHTFS_MOCK_GATEWAY_H
#define LIGHTFS_MOCK_GATEWAY_H

#include <stdint.h>

typedef struct {
    int is_put_called;
    int is_get_called;
    int is_delete_called;
    int is_list_called;
    uint32_t last_status;
    const char *last_bucket;
    const char *last_key;
    const void *last_data;
    uint32_t last_data_length;
} mock_gateway_state_t;

mock_gateway_state_t *mock_gateway_state(void);
void mock_gateway_reset(void);
void mock_gateway_set_response(uint32_t status, const void *data, uint32_t length);

#endif /* LIGHTFS_MOCK_GATEWAY_H */
