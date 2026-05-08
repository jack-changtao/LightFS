#ifndef LIGHTFS_MOCK_GATEWAY_H
#define LIGHTFS_MOCK_GATEWAY_H

#include <stdint.h>

typedef struct {
    int put_called;
    int get_called;
    int delete_called;
    int list_called;
    uint32_t last_status;
    const char *last_bucket;
    const char *last_key;
    const void *last_data;
    uint32_t last_data_len;
} mock_gateway_state_t;

mock_gateway_state_t *mock_gateway_state(void);
void mock_gateway_reset(void);
void mock_gateway_set_response(uint32_t status, const void *data, uint32_t len);

#endif /* LIGHTFS_MOCK_GATEWAY_H */
