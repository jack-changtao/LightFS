#include "http_response.h"
#include <string.h>
#include <stdio.h>

/* ── static iovec strings ────────────────────────────────────────── */

#define S(name, str) \
    static const struct iovec IOV_##name = { .iov_base = (char *)str, .iov_len = sizeof(str) - 1 }

S(HTTP_1_1,     "HTTP/1.1 ");
S(CRLF,         "\r\n");
S(CT_XML,       "Content-Type: application/xml\r\n");
S(CONN_KA,      "Connection: keep-alive\r\n");
S(CONN_CLOSE,   "Connection: close\r\n");

static const struct iovec status_table[] = {
    [200] = { .iov_base = "200 OK\r\n",          .iov_len = 9  },
    [204] = { .iov_base = "204 No Content\r\n",   .iov_len = 17 },
    [206] = { .iov_base = "206 Partial Content\r\n", .iov_len = 22 },
    [400] = { .iov_base = "400 Bad Request\r\n",  .iov_len = 18 },
    [403] = { .iov_base = "403 Forbidden\r\n",    .iov_len = 16 },
    [404] = { .iov_base = "404 Not Found\r\n",    .iov_len = 16 },
    [405] = { .iov_base = "405 Method Not Allowed\r\n", .iov_len = 25 },
    [413] = { .iov_base = "413 Payload Too Large\r\n", .iov_len = 24 },
    [500] = { .iov_base = "500 Internal Server Error\r\n", .iov_len = 27 },
    [503] = { .iov_base = "503 Service Unavailable\r\n", .iov_len = 25 },
};

/* scratch buffers for dynamic header values.
   Safe under SPDK single-threaded reactor model. */
static char etag_buf[S3_MAX_HEADER_LENGTH + 16];
static char cl_buf[64];
static char ct_buf[S3_MAX_HEADER_LENGTH + 32];

/* ── public ──────────────────────────────────────────────────────── */

int http_response_serialize(const s3_response_t *response,
                            bool keep_alive,
                            struct iovec *iov, int max_iov) {
    int n = 0;

    /* status line: "HTTP/1.1 " + status + CRLF */
    if (max_iov < 2) return -1;
    iov[n++] = IOV_HTTP_1_1;
    uint32_t status = response->http_status;
    if (status < sizeof(status_table) / sizeof(status_table[0]) && status_table[status].iov_base) {
        iov[n++] = status_table[status];
    } else {
        iov[n++] = (struct iovec){ .iov_base = "500 Internal Server Error\r\n", .iov_len = 27 };
    }

    /* Content-Type */
    if (max_iov - n < 1) return -1;
    if (response->content_type && response->content_type[0]) {
        int w = snprintf(ct_buf, sizeof(ct_buf), "Content-Type: %s\r\n",
                         response->content_type);
        iov[n++] = (struct iovec){ .iov_base = ct_buf, .iov_len = (size_t)w };
    } else if (response->body && response->body_length > 0) {
        iov[n++] = IOV_CT_XML;
    }

    /* ETag */
    if (response->etag[0]) {
        if (max_iov - n < 1) return -1;
        int w = snprintf(etag_buf, sizeof(etag_buf), "ETag: %s\r\n", response->etag);
        iov[n++] = (struct iovec){ .iov_base = etag_buf, .iov_len = (size_t)w };
    }

    /* Content-Length */
    if (response->body && response->body_length > 0) {
        if (max_iov - n < 1) return -1;
        int w = snprintf(cl_buf, sizeof(cl_buf), "Content-Length: %u\r\n", response->body_length);
        iov[n++] = (struct iovec){ .iov_base = cl_buf, .iov_len = (size_t)w };
    }

    /* Connection */
    if (max_iov - n < 1) return -1;
    iov[n++] = keep_alive ? IOV_CONN_KA : IOV_CONN_CLOSE;

    /* empty line */
    if (max_iov - n < 1) return -1;
    iov[n++] = IOV_CRLF;

    /* body (zero-copy pointer) */
    if (response->body && response->body_length > 0) {
        if (max_iov - n < 1) return -1;
        iov[n++] = (struct iovec){
            .iov_base = (void *)response->body,
            .iov_len = response->body_length
        };
    }

    return n;
}
