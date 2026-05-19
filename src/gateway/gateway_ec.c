#include "gateway_ec.h"
#include "lightfs/gateway/gateway_types.h"
#include <stdlib.h>
#include <string.h>

erasure_coding_params_t erasure_coding_params_for_policy(erasure_coding_policy_t policy) {
  erasure_coding_params_t params = {0};
  switch (policy) {
  case ERASURE_CODING_REPLICATION_2X:
    params.data_fragments = 1;
    params.parity_fragments = 1;
    params.stripe_size = 0;
    break;
  case ERASURE_CODING_REPLICATION_3X:
    params.data_fragments = 1;
    params.parity_fragments = 2;
    params.stripe_size = 0;
    break;
  case ERASURE_CODING_6_PLUS_3:
    params.data_fragments = 6;
    params.parity_fragments = 3;
    params.stripe_size = GATEWAY_MEDIUM_STRIPE_SIZE;
    break;
  case ERASURE_CODING_10_PLUS_4:
    params.data_fragments = 10;
    params.parity_fragments = 4;
    params.stripe_size = GATEWAY_MAX_STRIPE_SIZE;
    break;
  }
  return params;
}

erasure_coding_engine_t *erasure_coding_engine_create(int data_fragment_count, int parity_fragment_count) {
  if (data_fragment_count <= 0 || parity_fragment_count < 0 || data_fragment_count + parity_fragment_count > GATEWAY_MAX_FRAGMENTS) return NULL;

  erasure_coding_engine_t *engine = calloc(1, sizeof(erasure_coding_engine_t));
  if (!engine) return NULL;

  engine->data_fragment_count = data_fragment_count;
  engine->parity_fragment_count = parity_fragment_count;

  int total_fragments = data_fragment_count + parity_fragment_count;
  engine->encode_matrix = calloc(total_fragments * data_fragment_count, sizeof(uint8_t));
  if (!engine->encode_matrix) {
    free(engine);
    return NULL;
  }

  for (int i = 0; i < data_fragment_count; i++) {
    engine->encode_matrix[i * data_fragment_count + i] = 1;
  }
  for (int i = data_fragment_count; i < total_fragments; i++) {
    for (int j = 0; j < data_fragment_count; j++) {
      engine->encode_matrix[i * data_fragment_count + j] = (uint8_t)((i - data_fragment_count + 1) * (j + 1) & 0xFF);
    }
  }

  return engine;
}

void erasure_coding_engine_destroy(erasure_coding_engine_t *engine) {
  if (!engine) return;
  free(engine->encode_matrix);
  free(engine);
}

int erasure_coding_get_data_fragment_count(erasure_coding_engine_t *engine) { return engine ? engine->data_fragment_count : -1; }
int erasure_coding_get_parity_fragment_count(erasure_coding_engine_t *engine) { return engine ? engine->parity_fragment_count : -1; }

int erasure_coding_encode(erasure_coding_engine_t *engine, const uint8_t *data, uint64_t data_size,
       uint8_t **fragments_out, uint64_t *fragment_sizes) {
  if (!engine || !data || !fragments_out || !fragment_sizes) return -1;

  int data_fragment_count = engine->data_fragment_count;
  int total_fragments = data_fragment_count + engine->parity_fragment_count;

  uint64_t fragment_size = (data_size + data_fragment_count - 1) / data_fragment_count;
  fragment_size = ((fragment_size + 63) / 64) * 64;

  for (int i = 0; i < total_fragments; i++) {
    fragments_out[i] = calloc(1, fragment_size);
    if (!fragments_out[i]) {
      for (int j = 0; j < i; j++) free(fragments_out[j]);
      return -1;
    }
    fragment_sizes[i] = fragment_size;
  }

  for (int i = 0; i < data_fragment_count; i++) {
    uint64_t offset = i * fragment_size;
    uint64_t copy = (offset < data_size) ?
            (data_size - offset < fragment_size ? data_size - offset : fragment_size) : 0;
    if (copy > 0) memcpy(fragments_out[i], data + offset, copy);
  }

  for (int i = data_fragment_count; i < total_fragments; i++) {
    uint8_t *parity = fragments_out[i];
    for (int j = 0; j < data_fragment_count; j++) {
      uint8_t coefficient = engine->encode_matrix[i * data_fragment_count + j];
      if (coefficient == 0) continue;
      uint8_t *data_fragment = fragments_out[j];
      for (uint64_t b = 0; b < fragment_size; b++) {
        parity[b] ^= data_fragment[b];
      }
    }
  }

  return 0;
}

int erasure_coding_decode(erasure_coding_engine_t *engine,
       uint8_t **fragments, uint64_t *fragment_sizes,
       int *fragment_map, int fragment_count,
       uint8_t *data_out, uint64_t *data_size_out) {
  if (!engine || !fragments || !fragment_sizes || !fragment_map ||
    !data_out || !data_size_out) return -1;

  int data_fragment_count = engine->data_fragment_count;
  uint64_t fragment_size = fragment_sizes[0];

  for (int i = 0; i < data_fragment_count; i++) {
    int found = -1;
    for (int j = 0; j < fragment_count; j++) {
      if (fragment_map[j] == i) {
        found = j;
        break;
      }
    }
    if (found >= 0) {
      memcpy(data_out + i * fragment_size, fragments[found], fragment_size);
    }
  }

  *data_size_out = fragment_size * data_fragment_count;
  return 0;
}
