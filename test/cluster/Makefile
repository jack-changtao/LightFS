CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I../../src/cluster -I../../test/mocks -I../../src/storage -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

CLUSTER_LIB = ../../src/cluster/libcluster.a
MOCKS = ../mocks/mock_etcd.o

all: test_etcd_client test_cluster_node test_cluster_config test_cluster_discovery

../mocks/mock_etcd.o: ../mocks/mock_etcd.c ../mocks/mock_etcd.h
	cd ../mocks && $(CC) $(CFLAGS) -c mock_etcd.c -o mock_etcd.o

test_etcd_client: test_etcd_client.c $(CLUSTER_LIB) $(MOCKS)
	$(CC) $(CFLAGS) test_etcd_client.c $(MOCKS) $(CLUSTER_LIB) -o test_etcd_client

test_cluster_node: test_cluster_node.c $(CLUSTER_LIB) $(MOCKS)
	$(CC) $(CFLAGS) test_cluster_node.c $(MOCKS) $(CLUSTER_LIB) -o test_cluster_node

test_cluster_config: test_cluster_config.c $(CLUSTER_LIB) $(MOCKS)
	$(CC) $(CFLAGS) test_cluster_config.c $(MOCKS) $(CLUSTER_LIB) -o test_cluster_config

test_cluster_discovery: test_cluster_discovery.c $(CLUSTER_LIB) $(MOCKS)
	$(CC) $(CFLAGS) test_cluster_discovery.c $(MOCKS) $(CLUSTER_LIB) -o test_cluster_discovery

run: all
	@echo "=== test_etcd_client ==="
	./test_etcd_client
	@echo "=== test_cluster_node ==="
	./test_cluster_node
	@echo "=== test_cluster_config ==="
	./test_cluster_config
	@echo "=== test_cluster_discovery ==="
	./test_cluster_discovery

clean:
	rm -f test_etcd_client test_cluster_node test_cluster_config test_cluster_discovery
	rm -f ../mocks/*.o

.PHONY: all run clean
