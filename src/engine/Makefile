#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

# Build static library instead of app binary
LIB := storage_engine

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)

C_SRCS := \
	src/storage_engine.c \
	log/engine_log.c \
	src/shard.c \
	src/segment/alloc.c \
	src/segment/segment.c \
	src/segment/segment_cache.c \
	src/segment/update_queue.c \
	src/segment/reclaim.c \
	src/crc32c.c \
	src/superblock.c \
	src/shard_meta.c \
	src/wal.c \
	src/manifest.c \
	src/io.c \
	src/obj.c \
	src/gc.c \
	src/checkpoint.c \
	src/config.c

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk

CFLAGS += -I$(abspath .)/src
CFLAGS += -I$(abspath .)/log
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
