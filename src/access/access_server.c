#include "lightfs/access/access_server.h"
#include "lightfs/access/sigv4.h"
#include "lightfs/access/s3_xml.h"
#include "lightfs/access/access_types.h"
#include "access_routes.h"
#include "access_handlers.h"
#include <stdio.h>
#include <string.h>

static int dispatch_request(const char *method, const char *uri,
                             const char *headers[], int header_count,
                             const void *body, uint32_t body_len) {
    s3_request_t req = {0};
    int rc = s3_route_parse(method, uri, headers, header_count, &req);
    if (rc != 0) {
        printf("HTTP/1.1 400 Bad Request\r\n\r\n");
        char buf[512];
        int n = s3_xml_serialize_error(buf, sizeof(buf),
                                        "InvalidRequest", "Could not parse request");
        if (n > 0) fwrite(buf, 1, n, stdout);
        return -1;
    }

    if (req.authorization[0]) {
        sigv4_result_t auth = sigv4_validate(req.authorization, method, uri,
                                              req.host, req.date,
                                              body, body_len);
        if (auth != SIGV4_OK) {
            printf("HTTP/1.1 403 Forbidden\r\n\r\n");
            char buf[512];
            int n = s3_xml_serialize_error(buf, sizeof(buf),
                                            "SignatureDoesNotMatch",
                                            "The request signature does not match");
            if (n > 0) fwrite(buf, 1, n, stdout);
            return -1;
        }
    }

    s3_response_t resp = {0};
    switch (req.op) {
    case S3_OP_PUT_OBJECT:
        rc = s3_handler_put(&req, body, body_len, &resp);
        break;
    case S3_OP_GET_OBJECT:
        rc = s3_handler_get(&req, &resp);
        break;
    case S3_OP_DELETE_OBJECT:
        rc = s3_handler_delete(&req, &resp);
        break;
    case S3_OP_LIST_OBJECTS:
        rc = s3_handler_list(&req, &resp);
        break;
    default:
        printf("HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return -1;
    }

    if (rc != 0) {
        printf("HTTP/1.1 500 Internal Server Error\r\n\r\n");
        return -1;
    }

    printf("HTTP/1.1 %d OK\r\n", resp.http_status);
    if (resp.content_type) {
        printf("Content-Type: %s\r\n", resp.content_type);
    }
    if (resp.etag[0]) {
        printf("ETag: %s\r\n", resp.etag);
    }
    if (resp.body && resp.body_len > 0) {
        printf("Content-Length: %u\r\n", resp.body_len);
        printf("\r\n");
        fwrite(resp.body, 1, resp.body_len, stdout);
    } else {
        printf("\r\n");
    }

    return 0;
}

int access_server_start(const access_server_config_t *cfg) {
    if (!cfg) return -1;

    printf("Access Layer listening on %s:%d\n", cfg->listen_host, cfg->listen_port);

    /* Phase 2: stub — no actual HTTP server */

    return 0;
}

void access_server_stop(void) {
    /* Phase 3: stop HTTP listener */
}
