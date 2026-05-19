#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP := storage_engine_test

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)
ENGINE_DIR  := $(LIGHTFS_ROOT)/src/engine

C_SRCS := test_main.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

LIBS += -L$(ENGINE_DIR) -lstorage_engine

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

CFLAGS += -I$(ENGINE_DIR)/src
CFLAGS += -I$(ENGINE_DIR)/log
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
