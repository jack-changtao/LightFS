#include "access_handlers.h"
#include "lightfs/access/s3_xml.h"
#include "access_routes.h"
#include <string.h>
#include <stdio.h>

int s3_handler_put(const s3_request_t *request, const void *body, uint32_t body_length,
                    s3_response_t *response) {
    if (!request || !response) return -1;

    /* Phase 2: stub — forward to Gateway placeholder */
    (void)body; (void)body_length;

    response->http_status = 200;
    snprintf(response->etag, sizeof(response->etag), "\"d41d8cd98f00b204e9800998ecf8427e\"");
    response->content_type = "application/xml";

    return 0;
}

int s3_handler_get(const s3_request_t *request, s3_response_t *response) {
    if (!request || !response) return -1;

    static char placeholder[] = "placeholder_response_data";

    response->http_status = 200;
    response->body = placeholder;
    response->body_length = (uint32_t)sizeof(placeholder) - 1;
    response->content_type = "binary/octet-stream";

    return 0;
}

int s3_handler_delete(const s3_request_t *request, s3_response_t *response) {
    if (!request || !response) return -1;

    response->http_status = 204;
    response->body = NULL;
    response->body_length = 0;

    return 0;
}

int s3_handler_list(const s3_request_t *request, s3_response_t *response) {
    if (!request || !response) return -1;

    static char response_buffer[4096];
    const char *keys[] = {"example1.txt", "example2.txt"};

    int written = s3_xml_serialize_list_objects(response_buffer, sizeof(response_buffer),
                                           request->bucket, "", "", 1000,
                                           keys, 2, 0);
    if (written < 0) return -1;

    response->http_status = 200;
    response->body = response_buffer;
    response->body_length = (uint32_t)written;
    response->content_type = "application/xml";

    return 0;
}
