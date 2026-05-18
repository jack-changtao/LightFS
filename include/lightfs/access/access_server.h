#ifndef LIGHTFS_ACCESS_SERVER_H
#define LIGHTFS_ACCESS_SERVER_H

#include "lightfs/access/http_server.h"

int  access_server_start(const http_server_config_t *config);
void access_server_stop(void);

#endif /* LIGHTFS_ACCESS_SERVER_H */
