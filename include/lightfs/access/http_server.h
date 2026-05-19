#ifndef LIGHTFS_HTTP_SERVER_H
#define LIGHTFS_HTTP_SERVER_H

#include <stdint.h>

typedef struct {
  const char *listen_host;
  uint16_t    listen_port;
  uint32_t    max_connections;
  uint32_t    keep_alive_timeout_ms;
  uint32_t    request_timeout_ms;
  uint32_t    max_request_body;
  const char *tls_cert_path;
  const char *tls_key_path;
} http_server_config_t;

struct http_server;

int  http_server_start(const http_server_config_t *config, struct http_server **out_server);
void http_server_stop(struct http_server *server);

#endif /* LIGHTFS_HTTP_SERVER_H */
