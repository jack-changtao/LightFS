#include "lightfs/access/s3_xml.h"
#include <string.h>
#include <stdio.h>

int s3_xml_parse(const void *xml, uint32_t length) {
  /* Phase 2: stub — full XML parsing deferred to Phase 3 */
  (void)xml; (void)length;
  return 0;
}

int s3_xml_serialize_list_objects(void *buffer, int buffer_size,
                 const char *bucket,
                 const char *prefix,
                 const char *marker,
                 int max_keys,
                 const char **keys,
                 int key_count,
                 int is_truncated) {
  if (!buffer || buffer_size <= 0 || !bucket) return -1;

  char *cursor = (char *)buffer;
  int remaining = buffer_size;
  int written;

  written = snprintf(cursor, remaining,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ListBucketResult xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n"
        "<Name>%s</Name>"
        "<Prefix>%s</Prefix>"
        "<Marker>%s</Marker>"
        "<MaxKeys>%d</MaxKeys>",
        bucket, prefix, marker, max_keys);
  if (written < 0 || written >= remaining) return -1;
  cursor += written;
  remaining -= written;

  if (is_truncated) {
    written = snprintf(cursor, remaining,
          "<IsTruncated>true</IsTruncated>"
          "<NextMarker>%s</NextMarker>", marker);
  } else {
    written = snprintf(cursor, remaining, "<IsTruncated>false</IsTruncated>");
  }
  if (written < 0 || written >= remaining) return -1;
  cursor += written;
  remaining -= written;

  for (int i = 0; i < key_count; i++) {
    written = snprintf(cursor, remaining, "<Contents><Key>%s</Key></Contents>", keys[i]);
    if (written < 0 || written >= remaining) return -1;
    cursor += written;
    remaining -= written;
  }

  written = snprintf(cursor, remaining, "</ListBucketResult>\n");
  if (written < 0 || written >= remaining) return -1;
  cursor += written;
  remaining -= written;

  return (int)(cursor - (char *)buffer);
}

int s3_xml_serialize_error(void *buffer, int buffer_size,
              const char *code,
              const char *message) {
  if (!buffer || buffer_size <= 0 || !code || !message) return -1;

  return snprintf((char *)buffer, buffer_size,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<Error>"
          "<Code>%s</Code>"
          "<Message>%s</Message>"
          "</Error>\n",
          code, message);
}
