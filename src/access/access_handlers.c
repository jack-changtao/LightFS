#include "access_handlers.h"
#include "lightfs/access/s3_xml.h"
#include "access_routes.h"
#include <string.h>
#include <stdio.h>

int s3_handler_put(const s3_request_t *req, const void *body, uint32_t body_len,
                    s3_response_t *resp) {
    if (!req || !resp) return -1;

    /* Phase 2: stub — forward to Gateway placeholder */
    (void)body; (void)body_len;

    resp->http_status = 200;
    snprintf(resp->etag, sizeof(resp->etag), "\"d41d8cd98f00b204e9800998ecf8427e\"");
    resp->content_type = "application/xml";

    return 0;
}

int s3_handler_get(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    static char placeholder[] = "placeholder_response_data";

    resp->http_status = 200;
    resp->body = placeholder;
    resp->body_len = (uint32_t)sizeof(placeholder) - 1;
    resp->content_type = "binary/octet-stream";

    return 0;
}

int s3_handler_delete(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    resp->http_status = 204;
    resp->body = NULL;
    resp->body_len = 0;

    return 0;
}

int s3_handler_list(const s3_request_t *req, s3_response_t *resp) {
    if (!req || !resp) return -1;

    static char resp_buf[4096];
    const char *keys[] = {"example1.txt", "example2.txt"};

    int n = s3_xml_serialize_list_objects(resp_buf, sizeof(resp_buf),
                                           req->bucket, "", "", 1000,
                                           keys, 2, 0);
    if (n < 0) return -1;

    resp->http_status = 200;
    resp->body = resp_buf;
    resp->body_len = (uint32_t)n;
    resp->content_type = "application/xml";

    return 0;
}
