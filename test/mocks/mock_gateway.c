#include "mock_gateway.h"
#include <string.h>

static mock_gateway_state_t g_state = {0};
static uint32_t g_response_status = 0;
static char g_response_buffer[65536];
static uint32_t g_response_length = 0;

mock_gateway_state_t *mock_gateway_state(void) {
    return &g_state;
}

void mock_gateway_reset(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_response_status = 0;
    g_response_length = 0;
}

void mock_gateway_set_response(uint32_t status, const void *data, uint32_t length) {
    g_response_status = status;
    if (data && length > 0 && length < sizeof(g_response_buffer)) {
        memcpy(g_response_buffer, data, length);
        g_response_length = length;
    }
}
