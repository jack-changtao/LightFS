#include "config.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "engine_log.h"
#include "spdk/log.h"
#include <spdk/bdev.h>
#include <spdk/thread.h>
#include <spdk/json.h>
#include <spdk/util.h>

static config_t g_config;
static struct spdk_bdev *g_bdev = NULL;
static struct spdk_bdev_desc *g_bdev_desc = NULL;
static struct spdk_io_channel *g_io_channel = NULL;

struct json_bdev {
  char name[256];
  uint64_t size;
  uint32_t block_size;
  uint64_t num_blocks;
};

struct json_segment {
  int32_t max_segments;
  uint64_t segment_size;
  int32_t data_segments;
  int32_t journal_segments;
  int32_t metadata_segments;
};

struct json_wal {
  uint64_t ring_buffer_size;
};

struct json_gc {
  bool is_enabled;
  double threshold;
  int32_t interval;
  int32_t segment_scan_batch;
  uint64_t reschedule_delay_us;
};

struct json_checkpoint {
  bool is_enabled;
  int32_t interval;
};

struct json_shard {
  int32_t num_shards;
  int32_t bg_core;
  int32_t *core_map;
  int32_t core_map_len;
};

struct json_rpc {
  int32_t port;
  int32_t core_index;
};

struct json_log {
  char mode[16];
  char file_path[512];
  int32_t default_level;
};

struct json_debug {
  bool manifest_small_checkpoint;
};

#define CONFIG_PATH_MAX 512

static int
decode_string_buf256(const struct spdk_json_val *val, void *out)
{
  char *buf = out;

  if (val->type != SPDK_JSON_VAL_STRING) {
    return -1;
  }
  if (val->len >= 256) {
    return -1;
  }
  memcpy(buf, val->start, val->len);
  buf[val->len] = '\0';
  return 0;
}

static int
decode_string_cfg_path(const struct spdk_json_val *val, void *out)
{
  char *buf = out;

  if (val->type != SPDK_JSON_VAL_STRING) {
    return -1;
  }
  if (val->len >= CONFIG_PATH_MAX) {
    return -1;
  }
  memcpy(buf, val->start, val->len);
  buf[val->len] = '\0';
  return 0;
}

static int
decode_string_buf16(const struct spdk_json_val *val, void *out)
{
  char *buf = out;

  if (val->type != SPDK_JSON_VAL_STRING) {
    return -1;
  }
  if (val->len >= 16) {
    return -1;
  }
  memcpy(buf, val->start, val->len);
  buf[val->len] = '\0';
  return 0;
}

static int
decode_json_bool(const struct spdk_json_val *val, void *out)
{
  bool *b = out;
  if (val->type == SPDK_JSON_VAL_TRUE) {
    *b = true;
    return 0;
  }
  if (val->type == SPDK_JSON_VAL_FALSE) {
    *b = false;
    return 0;
  }
  return -1;
}

/*
 * SPDK 子系统 JSON 若为相对路径，则相对于「当前应用配置文件」所在目录解析，
 * 以便从任意 cwd 使用 ../../conf/foo.json 等形式加载应用配置。
 */
static int
resolve_spdk_json_path(const char *config_file, char *spdk_json, size_t cap)
{
  char tmp[CONFIG_PATH_MAX];
  const char *slash;
  size_t dirlen, plen;

  if (spdk_json[0] == '\0') {
    return -1;
  }
  if (spdk_json[0] == '/') {
    return 0;
  }
  slash = strrchr(config_file, '/');
  if (slash == NULL) {
    return 0;
  }
  dirlen = (size_t)(slash - config_file) + 1;
  plen = strlen(spdk_json);
  if (dirlen + plen + 1 > sizeof(tmp) || dirlen + plen + 1 > cap) {
    return -1;
  }
  memcpy(tmp, config_file, dirlen);
  memcpy(tmp + dirlen, spdk_json, plen + 1);
  memcpy(spdk_json, tmp, dirlen + plen + 1);
  return 0;
}

static int
decode_json_number_as_double(const struct spdk_json_val *val, void *out)
{
  double *d = out;
  char *tmp;
  char *endptr;

  if (val->type != SPDK_JSON_VAL_NUMBER) {
    return -1;
  }
  tmp = malloc(val->len + 1);
  if (!tmp) {
    return -1;
  }
  memcpy(tmp, val->start, val->len);
  tmp[val->len] = '\0';
  *d = strtod(tmp, &endptr);
  free(tmp);
  if (endptr == tmp) {
    return -1;
  }
  return 0;
}

static const struct spdk_json_object_decoder json_bdev_decoders[] = {
    {"name", offsetof(struct json_bdev, name), decode_string_buf256, false},
    {"size", offsetof(struct json_bdev, size), spdk_json_decode_uint64, true},
    {"block_size", offsetof(struct json_bdev, block_size), spdk_json_decode_uint32, true},
    {"num_blocks", offsetof(struct json_bdev, num_blocks), spdk_json_decode_uint64, true},
};

static const struct spdk_json_object_decoder json_segment_decoders[] = {
    {"max_segments", offsetof(struct json_segment, max_segments), spdk_json_decode_int32, true},
    {"segment_size", offsetof(struct json_segment, segment_size), spdk_json_decode_uint64, false},
    {"data_segments", offsetof(struct json_segment, data_segments), spdk_json_decode_int32, true},
    {"journal_segments", offsetof(struct json_segment, journal_segments), spdk_json_decode_int32, true},
    {"metadata_segments", offsetof(struct json_segment, metadata_segments), spdk_json_decode_int32, true},
};

static const struct spdk_json_object_decoder json_wal_decoders[] = {
    {"ring_buffer_size", offsetof(struct json_wal, ring_buffer_size), spdk_json_decode_uint64, false},
};

static const struct spdk_json_object_decoder json_gc_decoders[] = {
    {"enabled", offsetof(struct json_gc, is_enabled), decode_json_bool, true},
    {"threshold", offsetof(struct json_gc, threshold), decode_json_number_as_double, false},
    {"interval", offsetof(struct json_gc, interval), spdk_json_decode_int32, false},
    {"segment_scan_batch", offsetof(struct json_gc, segment_scan_batch), spdk_json_decode_int32, true},
    {"reschedule_delay_us", offsetof(struct json_gc, reschedule_delay_us), spdk_json_decode_uint64, true},
};

static const struct spdk_json_object_decoder json_checkpoint_decoders[] = {
    {"enabled", offsetof(struct json_checkpoint, is_enabled), decode_json_bool, true},
    {"interval", offsetof(struct json_checkpoint, interval), spdk_json_decode_int32, false},
};

/* Temporary context for core_map decoding — tracks both pointer and length */
static int32_t *g_json_core_map_values;
static int32_t g_json_core_map_len;

static int
json_decode_core_map(const struct spdk_json_val *val, void *out)
{
  int32_t **arr = out;
  int32_t count;
  struct spdk_json_val *element;

  /* val points to the ARRAY_BEGIN token; len is the number of elements */
  count = (int32_t)spdk_json_val_len(val);
  /* spdk_json_val_len returns elements + 2 (BEGIN + END), so subtract 2 */
  count -= 2;
  if (count <= 0 || count > 64) {
    return -EINVAL;
  }

  g_json_core_map_values = calloc((size_t)count, sizeof(int32_t));
  if (!g_json_core_map_values) {
    return -ENOMEM;
  }

  element = spdk_json_array_first((struct spdk_json_val *)val);
  for (int32_t i = 0; i < count; i++) {
    if (spdk_json_decode_int32(element, &g_json_core_map_values[i]) != 0) {
      free(g_json_core_map_values);
      g_json_core_map_values = NULL;
      return -EINVAL;
    }
    element = spdk_json_next(element);
  }

  *arr = g_json_core_map_values;
  g_json_core_map_len = count;
  return 0;
}

static const struct spdk_json_object_decoder json_shard_decoders[] = {
    {"num_shards", offsetof(struct json_shard, num_shards), spdk_json_decode_int32, true},
    {"bg_core", offsetof(struct json_shard, bg_core), spdk_json_decode_int32, true},
    {"core_map", offsetof(struct json_shard, core_map), json_decode_core_map, true},
};

static const struct spdk_json_object_decoder json_rpc_decoders[] = {
    {"port", offsetof(struct json_rpc, port), spdk_json_decode_int32, true},
    {"core_index", offsetof(struct json_rpc, core_index), spdk_json_decode_int32, true},
};

static const struct spdk_json_object_decoder json_log_decoders[] = {
    {"mode", offsetof(struct json_log, mode), decode_string_buf16, false},
    {"file_path", offsetof(struct json_log, file_path), decode_string_cfg_path, false},
    {"default_level", offsetof(struct json_log, default_level), spdk_json_decode_int32, false},
};

static const struct spdk_json_object_decoder json_debug_decoders[] = {
    {"manifest_small_checkpoint", offsetof(struct json_debug, manifest_small_checkpoint), decode_json_bool, false},
};

static int
read_file_to_buffer(const char *path, char **out_buf, size_t *out_len)
{
  FILE *f;
  long sz;
  char *buf;

  f = fopen(path, "rb");
  if (!f) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: cannot open %s: %s", path, strerror(errno));
    return -errno;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -EIO;
  }
  sz = ftell(f);
  if (sz < 0 || sz > (long)(4 * 1024 * 1024)) {
    fclose(f);
    return -EINVAL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -EIO;
  }
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -ENOMEM;
  }
  if (sz > 0 && fread(buf, (size_t)sz, 1, f) != 1) {
    free(buf);
    fclose(f);
    return -EIO;
  }
  fclose(f);
  buf[sz] = '\0';
  *out_buf = buf;
  *out_len = (size_t)sz;
  return 0;
}

config_t *
get_config(void)
{
  return &g_config;
}

int
load_config(const char *config_file)
{
  const char *path = (config_file != NULL && config_file[0] != '\0') ? config_file : "./conf/config.json";
  char *buf = NULL;
  size_t len = 0;
  struct spdk_json_val *values = NULL;
  void *end = NULL;
  ssize_t rc;
  uint32_t parse_flags = SPDK_JSON_PARSE_FLAG_ALLOW_COMMENTS | SPDK_JSON_PARSE_FLAG_DECODE_IN_PLACE;
  int ret = 0;
  struct spdk_json_val *v;
  struct json_bdev jb;
  struct json_segment js;
  struct json_wal jw;
  struct json_gc jg;
  struct json_checkpoint jc;
  struct json_shard jsh;
  struct json_log jl;

  memset(&g_config, 0, sizeof(g_config));
  g_config.gc.is_enabled = true;
  g_config.checkpoint.is_enabled = true;
  g_config.shard.num_shards = 4;
  g_config.shard.bg_core = 0;  /* 0 means not specified; defaults to num_shards later */
  g_config.shard.core_map = NULL;
  g_config.shard.core_map_len = 0;
  g_config.rpc.port = 0;
  g_config.rpc.core_index = 0;
  if (snprintf(g_config.log.mode, sizeof(g_config.log.mode), "%s", "console")
      >= (int)sizeof(g_config.log.mode)) {
    return -EINVAL;
  }
  if (snprintf(g_config.log.file_path, sizeof(g_config.log.file_path), "%s", "storage_engine.log")
      >= (int)sizeof(g_config.log.file_path)) {
    return -EINVAL;
  }
  g_config.log.default_level = 1;
  g_config.debug.manifest_small_checkpoint = false;

  ret = read_file_to_buffer(path, &buf, &len);
  if (ret != 0) {
    return ret;
  }

  rc = spdk_json_parse(buf, len, NULL, 0, &end, parse_flags);
  if (rc < 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: JSON parse (size pass) failed for %s rc=%zd", path, rc);
    ret = -EINVAL;
    goto out;
  }

  values = calloc((size_t)rc, sizeof(struct spdk_json_val));
  if (!values) {
    ret = -ENOMEM;
    goto out;
  }

  rc = spdk_json_parse(buf, len, values, (size_t)rc, &end, parse_flags);
  if (rc < 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: JSON parse failed for %s rc=%zd", path, rc);
    ret = -EINVAL;
    goto out;
  }

  if (rc < 1 || values[0].type != SPDK_JSON_VAL_OBJECT_BEGIN) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: root must be a JSON object: %s", path);
    ret = -EINVAL;
    goto out;
  }

  v = NULL;
  if (spdk_json_find(&values[0], "spdk", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) != 0 || v == NULL) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing \"spdk\" object in %s", path);
    ret = -EINVAL;
    goto out;
  }

  {
    struct spdk_json_val *spdk_root = v;
    struct spdk_json_val *json_path = NULL;
    struct spdk_json_val *bdev_obj = NULL;

    if (spdk_json_find(spdk_root, "json_config_file", NULL, &json_path, SPDK_JSON_VAL_STRING) == 0
        && json_path != NULL) {
      if (decode_string_cfg_path(json_path, g_config.spdk_json_config_file) != 0) {
        ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid spdk.json_config_file in %s", path);
        ret = -EINVAL;
        goto out;
      }
    } else {
      if (snprintf(g_config.spdk_json_config_file, sizeof(g_config.spdk_json_config_file), "%s",
                   "nvme.json")
          >= (int)sizeof(g_config.spdk_json_config_file)) {
        ret = -EINVAL;
        goto out;
      }
    }

    if (spdk_json_find(spdk_root, "bdev", NULL, &bdev_obj, SPDK_JSON_VAL_OBJECT_BEGIN) != 0
        || bdev_obj == NULL) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing spdk.bdev in %s", path);
      ret = -EINVAL;
      goto out;
    }
    memset(&jb, 0, sizeof(jb));
    if (spdk_json_decode_object(bdev_obj, json_bdev_decoders, SPDK_COUNTOF(json_bdev_decoders), &jb)
        != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid spdk.bdev in %s", path);
      ret = -EINVAL;
      goto out;
    }
    memcpy(&g_config.bdev, &jb, sizeof(g_config.bdev));
  }

  v = NULL;
  if (spdk_json_find(&values[0], "segment", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) != 0 || v == NULL) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing \"segment\" object in %s", path);
    ret = -EINVAL;
    goto out;
  }
  memset(&js, 0, sizeof(js));
  if (spdk_json_decode_object(v, json_segment_decoders, SPDK_COUNTOF(json_segment_decoders), &js)
      != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid segment section in %s", path);
    ret = -EINVAL;
    goto out;
  }
  if (js.segment_size == 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: segment.segment_size must be set and non-zero");
    ret = -EINVAL;
    goto out;
  }
  g_config.segment.segment_size = js.segment_size;
  if (js.max_segments > 0) {
    g_config.segment.max_segments = js.max_segments;
  }

  v = NULL;
  if (spdk_json_find(&values[0], "wal", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) != 0 || v == NULL) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing \"wal\" object in %s", path);
    ret = -EINVAL;
    goto out;
  }
  memset(&jw, 0, sizeof(jw));
  if (spdk_json_decode_object(v, json_wal_decoders, SPDK_COUNTOF(json_wal_decoders), &jw) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid wal section in %s", path);
    ret = -EINVAL;
    goto out;
  }
  g_config.wal.ring_buffer_size = jw.ring_buffer_size;

  v = NULL;
  if (spdk_json_find(&values[0], "gc", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) != 0 || v == NULL) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing \"gc\" object in %s", path);
    ret = -EINVAL;
    goto out;
  }
  memset(&jg, 0, sizeof(jg));
  jg.is_enabled = true;
  if (spdk_json_decode_object(v, json_gc_decoders, SPDK_COUNTOF(json_gc_decoders), &jg) != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid gc section in %s", path);
    ret = -EINVAL;
    goto out;
  }
  g_config.gc.is_enabled = jg.is_enabled;
  g_config.gc.threshold = jg.threshold;
  g_config.gc.interval = jg.interval;
  g_config.gc.segment_scan_batch = jg.segment_scan_batch;
  if (g_config.gc.segment_scan_batch <= 0) {
    g_config.gc.segment_scan_batch = 64;
  }
  if (g_config.gc.segment_scan_batch > 65536) {
    ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG, "config: gc.segment_scan_batch=%d capped to 65536",
             g_config.gc.segment_scan_batch);
    g_config.gc.segment_scan_batch = 65536;
  }

  g_config.gc.gc_reschedule_delay_us = jg.reschedule_delay_us;
  if (g_config.gc.gc_reschedule_delay_us == 0) {
    g_config.gc.gc_reschedule_delay_us = 100000;
  }

  v = NULL;
  if (spdk_json_find(&values[0], "checkpoint", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) != 0
      || v == NULL) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: missing \"checkpoint\" object in %s", path);
    ret = -EINVAL;
    goto out;
  }
  memset(&jc, 0, sizeof(jc));
  jc.is_enabled = true;
  if (spdk_json_decode_object(v, json_checkpoint_decoders, SPDK_COUNTOF(json_checkpoint_decoders),
                              &jc)
      != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid checkpoint section in %s", path);
    ret = -EINVAL;
    goto out;
  }
  g_config.checkpoint.is_enabled = jc.is_enabled;
  g_config.checkpoint.interval = jc.interval;

  v = NULL;
  if (spdk_json_find(&values[0], "shard", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) == 0 && v != NULL) {
    memset(&jsh, 0, sizeof(jsh));
    if (spdk_json_decode_object(v, json_shard_decoders, SPDK_COUNTOF(json_shard_decoders), &jsh) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid shard section in %s", path);
      ret = -EINVAL;
      goto out;
    }
    if (jsh.num_shards != 0) {
      g_config.shard.num_shards = jsh.num_shards;
    }
    if (jsh.bg_core != 0) {
      g_config.shard.bg_core = jsh.bg_core;
    }
    if (jsh.core_map != NULL) {
      g_config.shard.core_map = jsh.core_map;
      g_config.shard.core_map_len = g_json_core_map_len;
    }
  }
  if (g_config.shard.num_shards < 1 || g_config.shard.num_shards > 64) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: shard.num_shards must be in 1..64 (mkfs layout)");
    ret = -EINVAL;
    goto out;
  }

  /* Apply defaults for core_map and bg_core */
  if (g_config.shard.core_map == NULL) {
    g_config.shard.core_map_len = g_config.shard.num_shards;
    g_config.shard.core_map = calloc((size_t)g_config.shard.core_map_len, sizeof(int));
    if (!g_config.shard.core_map) {
      ret = -ENOMEM;
      goto out;
    }
    for (int i = 0; i < g_config.shard.core_map_len; i++) {
      g_config.shard.core_map[i] = i;
    }
  }
  if (g_config.shard.bg_core == 0) {
    g_config.shard.bg_core = g_config.shard.num_shards;
  }

  /* Validate core_map length matches num_shards */
  if (g_config.shard.core_map_len != g_config.shard.num_shards) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG,
      "config: shard.core_map length (%d) must equal shard.num_shards (%d)",
      g_config.shard.core_map_len, g_config.shard.num_shards);
    ret = -EINVAL;
    goto out;
  }

  v = NULL;
  if (spdk_json_find(&values[0], "rpc", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) == 0 && v != NULL) {
    struct json_rpc jr;
    memset(&jr, 0, sizeof(jr));
    if (spdk_json_decode_object(v, json_rpc_decoders, SPDK_COUNTOF(json_rpc_decoders), &jr) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid rpc section in %s", path);
      ret = -EINVAL;
      goto out;
    }
    g_config.rpc.port = jr.port;
    g_config.rpc.core_index = jr.core_index;
  }

  v = NULL;
  if (spdk_json_find(&values[0], "log", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) == 0 && v != NULL) {
    memset(&jl, 0, sizeof(jl));
    jl.default_level = -1;
    if (spdk_json_decode_object(v, json_log_decoders, SPDK_COUNTOF(json_log_decoders), &jl) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid log section in %s", path);
      ret = -EINVAL;
      goto out;
    }
    if (jl.mode[0] != '\0') {
      if (snprintf(g_config.log.mode, sizeof(g_config.log.mode), "%s", jl.mode)
          >= (int)sizeof(g_config.log.mode)) {
        ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: log.mode too long in %s", path);
        ret = -EINVAL;
        goto out;
      }
    }
    if (jl.file_path[0] != '\0') {
      if (snprintf(g_config.log.file_path, sizeof(g_config.log.file_path), "%s", jl.file_path)
          >= (int)sizeof(g_config.log.file_path)) {
        ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: log.file_path too long in %s", path);
        ret = -EINVAL;
        goto out;
      }
    }
    if (jl.default_level != -1) {
      g_config.log.default_level = jl.default_level;
    }
  }

  v = NULL;
  if (spdk_json_find(&values[0], "debug", NULL, &v, SPDK_JSON_VAL_OBJECT_BEGIN) == 0 && v != NULL) {
    struct json_debug jd;
    memset(&jd, 0, sizeof(jd));
    if (spdk_json_decode_object(v, json_debug_decoders, SPDK_COUNTOF(json_debug_decoders), &jd) != 0) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: invalid debug section in %s", path);
      ret = -EINVAL;
      goto out;
    }
    g_config.debug.manifest_small_checkpoint = jd.manifest_small_checkpoint;
  }

  if (strcmp(g_config.log.mode, "console") != 0 && strcmp(g_config.log.mode, "file") != 0
      && strcmp(g_config.log.mode, "both") != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: log.mode must be one of console|file|both");
    ret = -EINVAL;
    goto out;
  }
  if (g_config.log.default_level < 0 || g_config.log.default_level > 4) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: log.default_level must be in 0..4");
    ret = -EINVAL;
    goto out;
  }

  if (resolve_spdk_json_path(path, g_config.spdk_json_config_file,
                             sizeof(g_config.spdk_json_config_file))
      != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: spdk json path resolve failed (too long?) for %s", path);
    ret = -EINVAL;
    goto out;
  }

  ENG_LOG_INFO(ENGINE_LOG_MOD_CONFIG, "Loaded config from %s (bdev %s, spdk json %s)", path,
           g_config.bdev.name, g_config.spdk_json_config_file);

out:
  free(values);
  free(buf);
  return ret;
}

static void
bdev_event_callback(enum spdk_bdev_event_type type, struct spdk_bdev *bdev, void *event_ctx)
{
  (void)event_ctx;
  switch (type) {
  case SPDK_BDEV_EVENT_REMOVE:
    ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG, "Bdev %s removed", spdk_bdev_get_name(bdev));
    if (g_io_channel) {
      spdk_put_io_channel(g_io_channel);
      g_io_channel = NULL;
    }
    if (g_bdev_desc) {
      spdk_bdev_close(g_bdev_desc);
      g_bdev_desc = NULL;
    }
    g_bdev = NULL;
    break;
  default:
    ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG, "Unknown bdev event type: %d", type);
    break;
  }
}

int
init_spdk_bdev(void)
{
  struct spdk_bdev *bdev;
  struct spdk_bdev_desc *desc;
  int rc;

  bdev = spdk_bdev_get_by_name(g_config.bdev.name);
  if (!bdev) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "Failed to find bdev: %s", g_config.bdev.name);
    SPDK_ERRLOG("Could not find bdev: %s\n", g_config.bdev.name);
    return -1;
  }

  rc = spdk_bdev_open_ext(g_config.bdev.name, true, bdev_event_callback, NULL, &desc);
  if (rc != 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "Failed to open bdev: %s, rc=%d", g_config.bdev.name, rc);
    SPDK_ERRLOG("Could not open bdev: %s\n", g_config.bdev.name);
    return rc;
  }
  g_bdev = bdev;
  g_bdev_desc = desc;

  g_config.bdev.block_size = spdk_bdev_get_block_size(g_bdev);
  g_config.bdev.num_blocks = spdk_bdev_get_num_blocks(g_bdev);
  g_config.bdev.size = g_config.bdev.num_blocks * (uint64_t)g_config.bdev.block_size;

  ENG_LOG_INFO(ENGINE_LOG_MOD_CONFIG,
           "Successfully int spdk bdev: %s (block_size=%u num_blocks=%ju size_bytes=%ju)",
           spdk_bdev_get_name(g_bdev), (unsigned)g_config.bdev.block_size,
           (uintmax_t)g_config.bdev.num_blocks, (uintmax_t)g_config.bdev.size);

  if (g_config.segment.segment_size == 0) {
    ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG, "config: segment.segment_size is 0 after load_config");
    spdk_bdev_close(desc);
    g_bdev_desc = NULL;
    g_bdev = NULL;
    return -1;
  }
  {
    uint64_t cap = g_config.bdev.size;
    uint64_t seg_sz = g_config.segment.segment_size;
    uint64_t num_segments = cap / seg_sz;

    /* Respect user-configured max_segments if it's smaller than bdev capacity */
    if (g_config.segment.max_segments > 0 && (uint64_t)g_config.segment.max_segments < num_segments) {
      num_segments = (uint64_t)g_config.segment.max_segments;
    }

    if (cap % seg_sz != 0) {
      ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG,
               "config: warning: bdev size %" PRIu64 " is not a multiple of segment_size %" PRIu64
               " (%" PRIu64 " byte tail unused)",
               cap, seg_sz, cap % seg_sz);
    }
    if (num_segments == 0 || num_segments > (uint64_t)INT_MAX) {
      ENG_LOG_ERROR(ENGINE_LOG_MOD_CONFIG,
               "config: derived max_segments=%" PRIu64 " invalid (bdev bytes / segment_size); "
               "adjust segment_size or device capacity",
               num_segments);
      spdk_bdev_close(desc);
      g_bdev_desc = NULL;
      g_bdev = NULL;
      return -1;
    }
    g_config.segment.max_segments = (int)num_segments;
    g_config.segment.data_segments = 0;
    g_config.segment.journal_segments = 0;
    g_config.segment.metadata_segments = 0;
    ENG_LOG_INFO(ENGINE_LOG_MOD_CONFIG, "config: segment max_segments=%d (bdev size / segment_size)",
             g_config.segment.max_segments);
  }

  return 0;
}

void
fini_spdk_bdev(void)
{
  struct spdk_thread *io_thread = g_io_channel ? spdk_io_channel_get_thread(g_io_channel) : NULL;
  struct spdk_thread *cur = spdk_get_thread();

  if (g_io_channel) {
    /*
     * spdk_put_io_channel must be called from the same thread that owns
     * the io_channel. If we're on a different thread, skip and let SPDK
     * clean up automatically during app stop.
     */
    if (io_thread && cur && io_thread != cur) {
      ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG,
               "fini_spdk_bdev: called from wrong thread, skipping put_io_channel");
    } else {
      spdk_put_io_channel(g_io_channel);
    }
    g_io_channel = NULL;
  }
  if (g_bdev_desc) {
    /*
     * spdk_bdev_close may acquire spinlocks that require being called from
     * an SPDK thread. If we're not on a valid SPDK thread, skip and let
     * SPDK clean up during app stop.
     */
    if (!cur) {
      ENG_LOG_WARN(ENGINE_LOG_MOD_CONFIG,
               "fini_spdk_bdev: not on SPDK thread, skipping bdev_close");
    } else {
      spdk_bdev_close(g_bdev_desc);
    }
    g_bdev_desc = NULL;
  }
  g_bdev = NULL;
}

void
set_bdev(struct spdk_bdev *bdev)
{
  g_bdev = bdev;
}

struct spdk_bdev *
get_bdev(void)
{
  return g_bdev;
}

struct spdk_bdev_desc *
get_bdev_desc(void)
{
  return g_bdev_desc;
}
