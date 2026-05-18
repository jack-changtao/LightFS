#include "lightfs/access/sigv4.h"
#include <string.h>
#include <stdio.h>

static const char *CREDENTIAL_PREFIX = "AWS4-HMAC-SHA256 Credential=";

sigv4_result_t sigv4_validate(const char *auth_header,
                               const char *method,
                               const char *uri,
                               const char *host,
                               const char *date,
                               const void *body,
                               uint32_t body_length) {
    if (!auth_header) return SIGV4_ERROR_MISSING_HEADER;

    if (strncmp(auth_header, "AWS4-HMAC-SHA256", 16) != 0) {
        return SIGV4_ERROR_INVALID_CREDENTIAL;
    }

    const char *cred_start = strstr(auth_header, CREDENTIAL_PREFIX);
    if (!cred_start) return SIGV4_ERROR_INVALID_CREDENTIAL;

    const char *sig_start = strstr(auth_header, "Signature=");
    if (!sig_start) return SIGV4_ERROR_MISSING_HEADER;

    /* Phase 2: format validation only. Full signature verification
     * deferred to Phase 3 when credential store is available. */
    (void)method; (void)uri; (void)host; (void)date;
    (void)body; (void)body_length; (void)sig_start;

    return SIGV4_OK;
}

const char *sigv4_extract_access_key(const char *auth_header) {
    if (!auth_header) return NULL;

    const char *cred_start = strstr(auth_header, CREDENTIAL_PREFIX);
    if (!cred_start) return NULL;
    cred_start += strlen(CREDENTIAL_PREFIX);

    static char key_buffer[64];
    int i = 0;
    while (cred_start[i] && cred_start[i] != '/' && i < 63) {
        key_buffer[i] = cred_start[i];
        i++;
    }
    key_buffer[i] = '\0';
    return key_buffer;
}

const char *sigv4_extract_date(const char *date_header) {
    return date_header;
}
