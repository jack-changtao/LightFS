#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <spdk/bdev.h>

// 配置结构
typedef struct {
  /* SPDK 子系统 JSON（传给 spdk_app_opts.json_config_file），如 ./conf/nvme.json */
  char spdk_json_config_file[512];
  struct {
    char name[256];
    /* Filled by init_spdk_bdev() from the opened SPDK bdev (not from JSON). */
    uint64_t size;
    uint32_t block_size;
    uint64_t num_blocks;
  } bdev;
  struct {
    /* max_segments = bdev.size / segment_size after init_spdk_bdev(); not from JSON. */
    int max_segments;
    uint64_t segment_size;
    /* Reserved; not used by engine (kept zero). */
    int data_segments;
    int journal_segments;
    int metadata_segments;
  } segment;
  struct {
    uint64_t ring_buffer_size;
  } wal;
  struct {
    bool is_enabled;
    double threshold;
    int interval;
    int segment_scan_batch; /* batched GC: segment slots per event; 0 in JSON → default at load */
    uint64_t gc_reschedule_delay_us; /* delay before GC reschedule, microseconds; 0 → immediate */
  } gc;
  struct {
    bool is_enabled;
    int interval;
  } checkpoint;
  struct {
    /* sink mode string from config: console|file|both */
    char mode[16];
    /* output file path used when mode is file/both */
    char file_path[512];
    /* default log level: ENGINE_LOG_DEBUG(0) .. ENGINE_LOG_FATAL(4) */
    int default_level;
  } log;
  struct {
    int num_shards;
    int bg_core;
    int *core_map;
    int core_map_len;
  } shard;
  struct {
    int port;
    int core_index;
  } rpc;
  struct {
    bool manifest_small_checkpoint;
  } debug;
} config_t;

// 加载配置文件
int load_config(const char *config_file);

// 获取配置
config_t *get_config(void);

// 初始化SPDK bdev
int init_spdk_bdev(void);

// 释放 init_spdk_bdev 打开的 channel / 描述符（可重复调用）
void fini_spdk_bdev(void);

// 设置bdev设备
void set_bdev(struct spdk_bdev *bdev);

// 获取bdev设备
struct spdk_bdev *get_bdev(void);

// 获取bdev描述符
struct spdk_bdev_desc *get_bdev_desc(void);

#endif // CONFIG_H