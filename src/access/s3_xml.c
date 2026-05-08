#include "lightfs/access/s3_xml.h"
#include <string.h>
#include <stdio.h>

int s3_xml_parse(const void *xml, uint32_t len) {
    /* Phase 2: stub — full XML parsing deferred to Phase 3 */
    (void)xml; (void)len;
    return 0;
}

int s3_xml_serialize_list_objects(void *buf, int buf_size,
                                   const char *bucket,
                                   const char *prefix,
                                   const char *marker,
                                   int max_keys,
                                   const char **keys,
                                   int key_count,
                                   int truncated) {
    if (!buf || buf_size <= 0 || !bucket) return -1;

    char *p = (char *)buf;
    int remaining = buf_size;
    int n;

    n = snprintf(p, remaining,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n"
                 "<Name>%s</Name>"
                 "<Prefix>%s</Prefix>"
                 "<Marker>%s</Marker>"
                 "<MaxKeys>%d</MaxKeys>",
                 bucket, prefix, marker, max_keys);
    if (n < 0 || n >= remaining) return -1;
    p += n;
    remaining -= n;

    if (truncated) {
        n = snprintf(p, remaining,
                     "<IsTruncated>true</IsTruncated>"
                     "<NextMarker>%s</NextMarker>", marker);
    } else {
        n = snprintf(p, remaining, "<IsTruncated>false</IsTruncated>");
    }
    if (n < 0 || n >= remaining) return -1;
    p += n;
    remaining -= n;

    for (int i = 0; i < key_count; i++) {
        n = snprintf(p, remaining, "<Contents><Key>%s</Key></Contents>", keys[i]);
        if (n < 0 || n >= remaining) return -1;
        p += n;
        remaining -= n;
    }

    n = snprintf(p, remaining, "</ListBucketResult>\n");
    if (n < 0 || n >= remaining) return -1;
    p += n;
    remaining -= n;

    return (int)(p - (char *)buf);
}

int s3_xml_serialize_error(void *buf, int buf_size,
                            const char *code,
                            const char *message) {
    if (!buf || buf_size <= 0 || !code || !message) return -1;

    return snprintf((char *)buf, buf_size,
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<Error>"
                    "<Code>%s</Code>"
                    "<Message>%s</Message>"
                    "</Error>\n",
                    code, message);
}
