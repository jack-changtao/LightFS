CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/storage -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

TEST_SRC = test_storage.c
STORAGE_LIB = ../../src/storage/libstorage.a

all: test_storage

test_storage: $(TEST_SRC) $(STORAGE_LIB)
	$(CC) $(CFLAGS) $(TEST_SRC) $(STORAGE_LIB) -o test_storage

run: test_storage
	./test_storage

clean:
	rm -f test_storage

.PHONY: all run clean
