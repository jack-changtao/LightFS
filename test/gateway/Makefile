CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/meta -I../../src/storage \
          -I../../src/cluster -I../../src/gateway -I../../test/mocks -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

GATEWAY_LIB = ../../src/gateway/libgateway.a
CLUSTER_LIB = ../../src/cluster/libcluster.a
META_LIB = ../../src/meta/libmeta.a
STORAGE_LIB = ../../src/storage/libstorage.a
MOCKS = ../mocks/mock_etcd.o
LIBS = $(GATEWAY_LIB) $(CLUSTER_LIB) $(META_LIB) $(STORAGE_LIB)

all: test_gateway test_gateway_ec test_gateway_placement \
     test_gateway_manifest_cache test_gateway_replication

../mocks/mock_etcd.o: ../mocks/mock_etcd.c ../mocks/mock_etcd.h
	cd ../mocks && $(CC) $(CFLAGS) -c mock_etcd.c -o mock_etcd.o

test_gateway: test_gateway.c $(LIBS) $(MOCKS)
	$(CC) $(CFLAGS) test_gateway.c $(MOCKS) $(LIBS) -o test_gateway

test_gateway_ec: test_gateway_ec.c $(LIBS)
	$(CC) $(CFLAGS) test_gateway_ec.c $(LIBS) -o test_gateway_ec

test_gateway_placement: test_gateway_placement.c $(LIBS) $(MOCKS)
	$(CC) $(CFLAGS) test_gateway_placement.c $(MOCKS) $(LIBS) -o test_gateway_placement

test_gateway_manifest_cache: test_gateway_manifest_cache.c $(LIBS)
	$(CC) $(CFLAGS) test_gateway_manifest_cache.c $(LIBS) -o test_gateway_manifest_cache

test_gateway_replication: test_gateway_replication.c $(LIBS) $(MOCKS)
	$(CC) $(CFLAGS) test_gateway_replication.c $(MOCKS) $(LIBS) -o test_gateway_replication

run: all
	@echo "=== test_gateway ==="
	./test_gateway
	@echo "=== test_gateway_ec ==="
	./test_gateway_ec
	@echo "=== test_gateway_placement ==="
	./test_gateway_placement
	@echo "=== test_gateway_manifest_cache ==="
	./test_gateway_manifest_cache
	@echo "=== test_gateway_replication ==="
	./test_gateway_replication

clean:
	rm -f test_gateway test_gateway_ec test_gateway_placement \
	      test_gateway_manifest_cache test_gateway_replication
	rm -f ../mocks/*.o

.PHONY: all run clean
