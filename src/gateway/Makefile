CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/meta -I../../src/cluster -I../../src/storage -I. -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

SRCS = gateway.c gateway_ec.c gateway_placement.c gateway_fragment_router.c \
       gateway_manifest_cache.c gateway_replication.c
OBJ = $(SRCS:.c=.o)
TARGET = libgateway.a

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
