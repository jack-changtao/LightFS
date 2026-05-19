#ifndef LIGHTFS_S3_XML_H
#define LIGHTFS_S3_XML_H

#include <stdint.h>

int s3_xml_parse(const void *xml, uint32_t length);

int s3_xml_serialize_list_objects(void *buffer, int buffer_size,
                 const char *bucket,
                 const char *prefix,
                 const char *marker,
                 int max_keys,
                 const char **keys,
                 int key_count,
                 int is_truncated);

int s3_xml_serialize_error(void *buffer, int buffer_size,
              const char *code,
              const char *message);

#endif /* LIGHTFS_S3_XML_H */
