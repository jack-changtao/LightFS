#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)

# Include SPDK common for compiler flags and config
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)

LIB := libstorage_engine.a

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

CFLAGS += -I$(abspath .)/src
CFLAGS += -I$(abspath .)/log
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src

OBJS := $(C_SRCS:.c=.o)

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(Q)ar rcs $@ $^

%.o: %.c
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(Q)rm -f $(LIB) $(OBJS)
