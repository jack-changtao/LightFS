#include "gateway_fragment_router.h"
#include <stdlib.h>
#include <string.h>

struct fragment_router {
    service_discovery_t *discovery;
};

fragment_router_t *fragment_router_create(service_discovery_t *discovery) {
    if (!discovery) return NULL;
    fragment_router_t *router = calloc(1, sizeof(fragment_router_t));
    if (!router) return NULL;
    router->discovery = discovery;
    return router;
}

void fragment_router_destroy(fragment_router_t *router) {
    free(router);
}

int fragment_router_send(fragment_router_t *router,
                          fragment_t *fragments, int fragment_count,
                          int max_retries) {
    if (!router || !fragments || fragment_count <= 0) return -1;
    (void)max_retries;

    static uint64_t fake_segment = 1000;
    for (int i = 0; i < fragment_count; i++) {
        fragments[i].location.segment_id = fake_segment++;
        fragments[i].location.offset = i * 4096;
        fragments[i].location.size = fragments[i].size;
        fragments[i].location.checksum = 0;
    }
    return 0;
}

int fragment_router_read(fragment_router_t *router,
                          blob_location_t *locations, int count,
                          uint8_t **data_out, uint32_t *sizes_out) {
    if (!router || !locations || !data_out || !sizes_out) return -1;
    (void)count;
    return 0;
}
