#include "lightfs/access/access_server.h"
#include "http_server.h"
#include <stdio.h>

static struct http_server *g_server;

int access_server_start(const http_server_config_t *config) {
    if (!config) return -1;

    printf("Access Layer starting on %s:%d\n", config->listen_host, config->listen_port);

    int result = http_server_start(config, &g_server);
    if (result != 0) {
        printf("Access Layer: failed to start HTTP server\n");
        return -1;
    }

    printf("Access Layer: HTTP server started\n");
    return 0;
}

void access_server_stop(void) {
    printf("Access Layer: stopping HTTP server\n");
    http_server_stop(g_server);
    g_server = NULL;
}
