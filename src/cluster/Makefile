CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../test/mocks -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

SRCS = etcd_client.c cluster_node.c cluster_config.c cluster_discovery.c
OBJ = $(SRCS:.c=.o)
TARGET = libcluster.a

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
