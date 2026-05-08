#ifndef LIGHTFS_S3_XML_H
#define LIGHTFS_S3_XML_H

#include <stdint.h>

int s3_xml_parse(const void *xml, uint32_t len);

int s3_xml_serialize_list_objects(void *buf, int buf_size,
                                   const char *bucket,
                                   const char *prefix,
                                   const char *marker,
                                   int max_keys,
                                   const char **keys,
                                   int key_count,
                                   int truncated);

int s3_xml_serialize_error(void *buf, int buf_size,
                            const char *code,
                            const char *message);

#endif /* LIGHTFS_S3_XML_H */
