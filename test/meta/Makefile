CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/storage -I../../src/meta -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

STORAGE_LIB = ../../src/storage/libstorage.a
META_LIB = ../../src/meta/libmeta.a

all: test_meta_server test_meta_shard test_meta_checkpoint test_meta_recovery

test_meta_server: test_meta_server.c $(META_LIB) $(STORAGE_LIB)
	$(CC) $(CFLAGS) test_meta_server.c $(META_LIB) $(STORAGE_LIB) -o test_meta_server

test_meta_shard: test_meta_shard.c $(META_LIB) $(STORAGE_LIB)
	$(CC) $(CFLAGS) test_meta_shard.c $(META_LIB) $(STORAGE_LIB) -o test_meta_shard

test_meta_checkpoint: test_meta_checkpoint.c $(META_LIB) $(STORAGE_LIB)
	$(CC) $(CFLAGS) test_meta_checkpoint.c $(META_LIB) $(STORAGE_LIB) -o test_meta_checkpoint

test_meta_recovery: test_meta_recovery.c $(META_LIB) $(STORAGE_LIB)
	$(CC) $(CFLAGS) test_meta_recovery.c $(META_LIB) $(STORAGE_LIB) -o test_meta_recovery

run: all
	@echo "=== test_meta_server ==="
	./test_meta_server
	@echo "=== test_meta_shard ==="
	./test_meta_shard
	@echo "=== test_meta_checkpoint ==="
	./test_meta_checkpoint
	@echo "=== test_meta_recovery ==="
	./test_meta_recovery

clean:
	rm -f test_meta_server test_meta_shard test_meta_checkpoint test_meta_recovery

.PHONY: all run clean
