#include "mock_gateway.h"
#include <string.h>

static mock_gateway_state_t g_state = {0};
static uint32_t g_response_status = 0;
static char g_response_buf[65536];
static uint32_t g_response_len = 0;

mock_gateway_state_t *mock_gateway_state(void) {
    return &g_state;
}

void mock_gateway_reset(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_response_status = 0;
    g_response_len = 0;
}

void mock_gateway_set_response(uint32_t status, const void *data, uint32_t len) {
    g_response_status = status;
    if (data && len > 0 && len < sizeof(g_response_buf)) {
        memcpy(g_response_buf, data, len);
        g_response_len = len;
    }
}
