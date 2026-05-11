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
                             const void *body, uint32_t body_length) {
    s3_request_t request = {0};
    int result = s3_route_parse(method, uri, headers, header_count, &request);
    if (result != 0) {
        printf("HTTP/1.1 400 Bad Request\r\n\r\n");
        char buffer[512];
        int written = s3_xml_serialize_error(buffer, sizeof(buffer),
                                        "InvalidRequest", "Could not parse request");
        if (written > 0) fwrite(buffer, 1, written, stdout);
        return -1;
    }

    if (request.authorization[0]) {
        sigv4_result_t auth = sigv4_validate(request.authorization, method, uri,
                                              request.host, request.date,
                                              body, body_length);
        if (auth != SIGV4_ERROR_OK) {
            printf("HTTP/1.1 403 Forbidden\r\n\r\n");
            char buffer[512];
            int written = s3_xml_serialize_error(buffer, sizeof(buffer),
                                            "SignatureDoesNotMatch",
                                            "The request signature does not match");
            if (written > 0) fwrite(buffer, 1, written, stdout);
            return -1;
        }
    }

    s3_response_t response = {0};
    switch (request.operation) {
    case S3_OPERATION_PUT_OBJECT:
        result = s3_handler_put(&request, body, body_length, &response);
        break;
    case S3_OPERATION_GET_OBJECT:
        result = s3_handler_get(&request, &response);
        break;
    case S3_OPERATION_DELETE_OBJECT:
        result = s3_handler_delete(&request, &response);
        break;
    case S3_OPERATION_LIST_OBJECTS:
        result = s3_handler_list(&request, &response);
        break;
    default:
        printf("HTTP/1.1 405 Method Not Allowed\r\n\r\n");
        return -1;
    }

    if (result != 0) {
        printf("HTTP/1.1 500 Internal Server Error\r\n\r\n");
        return -1;
    }

    printf("HTTP/1.1 %d OK\r\n", response.http_status);
    if (response.content_type) {
        printf("Content-Type: %s\r\n", response.content_type);
    }
    if (response.etag[0]) {
        printf("ETag: %s\r\n", response.etag);
    }
    if (response.body && response.body_length > 0) {
        printf("Content-Length: %u\r\n", response.body_length);
        printf("\r\n");
        fwrite(response.body, 1, response.body_length, stdout);
    } else {
        printf("\r\n");
    }

    return 0;
}

int access_server_start(const access_server_config_t *config) {
    if (!config) return -1;

    printf("Access Layer listening on %s:%d\n", config->listen_host, config->listen_port);

    /* Phase 2: stub — no actual HTTP server */

    return 0;
}

void access_server_stop(void) {
    /* Phase 3: stop HTTP listener */
}
