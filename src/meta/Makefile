CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/storage -I. -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

SRCS = meta_server.c meta_shard.c meta_checkpoint.c meta_recovery.c meta_bucket_registry.c
OBJ = $(SRCS:.c=.o)
TARGET = libmeta.a

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
