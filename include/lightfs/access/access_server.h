#ifndef LIGHTFS_ACCESS_SERVER_H
#define LIGHTFS_ACCESS_SERVER_H

#include <stdint.h>

typedef struct {
    const char *listen_host;
    uint16_t listen_port;
    uint32_t max_request_body;
} access_server_config_t;

int access_server_start(const access_server_config_t *cfg);
void access_server_stop(void);

#endif /* LIGHTFS_ACCESS_SERVER_H */
