#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2022 Intel Corporation.
#  All rights reserved.
#
SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP := storage_server

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)
ENGINE_DIR := $(LIGHTFS_ROOT)/src/engine
RPC_DIR    := $(LIGHTFS_ROOT)/src/rpc

C_SRCS := \
	storage_server.c \
	rpc_handler.c \
	callback_bridge.c \
	$(RPC_DIR)/src/server.c \
	$(RPC_DIR)/src/frame.c \
	$(RPC_DIR)/src/transport_tcp.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

# Link against libstorage_engine.a
LIBS += -L$(ENGINE_DIR) -lstorage_engine

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

CFLAGS += -I$(ENGINE_DIR)/src
CFLAGS += -I$(ENGINE_DIR)/log
CFLAGS += -I$(RPC_DIR)/src
CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
