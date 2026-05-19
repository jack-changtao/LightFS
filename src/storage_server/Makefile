SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
CFLAGS += -I$(LIGHTFS_ROOT)/src/rpc/src
CFLAGS += -I$(LIGHTFS_ROOT)/src/engine/src

LIB = libstorageserver.a
OBJS = storage_server.o rpc_handler.o callback_bridge.o

all: $(LIB)

$(LIB): $(OBJS)
	$(Q)ar rcs $@ $^

storage_server.o: storage_server.c storage_server_internal.h \
    $(abspath $(CURDIR)/../../include/lightfs/storage_server/storage_server.h)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

rpc_handler.o: rpc_handler.c storage_server_internal.h
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

callback_bridge.o: callback_bridge.c storage_server_internal.h
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB) $(OBJS)

.PHONY: all clean
