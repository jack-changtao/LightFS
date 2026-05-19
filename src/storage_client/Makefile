SPDK_ROOT_DIR ?= $(abspath $(HOME)/spdk)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

LIGHTFS_ROOT := $(shell cd $(CURDIR)/../.. && pwd)

CFLAGS += -I$(LIGHTFS_ROOT)/include
CFLAGS += -I$(LIGHTFS_ROOT)/src
CFLAGS += -I$(LIGHTFS_ROOT)/src/rpc/src
CFLAGS += -I$(LIGHTFS_ROOT)/src/engine/src

LIB = libstorageclient.a
OBJS = storage_client.o

all: $(LIB)

$(LIB): $(OBJS)
	$(Q)ar rcs $@ $^

storage_client.o: storage_client.c storage_client_internal.h \
    $(abspath $(CURDIR)/../../include/lightfs/storage_client/storage_client.h) \
    $(abspath $(CURDIR)/../../include/lightfs/storage_server/storage_server.h)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB) $(OBJS)

.PHONY: all clean
