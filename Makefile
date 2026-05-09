# LightFS top-level Makefile
# Phase 1: Storage, Phase 2: Access, Phase 3: Meta, Phase 4: Cluster, Phase 5: Gateway

.PHONY: all clean test access storage meta cluster gateway

# Default target
all: access storage meta cluster gateway

# Build Access layer
access:
	$(MAKE) -C src/access

# Build Storage engine
storage:
	$(MAKE) -C src/storage

# Build Meta Server
meta:
	$(MAKE) -C src/meta

# Build Cluster/etcd
cluster:
	$(MAKE) -C src/cluster

# Build Gateway
gateway:
	$(MAKE) -C src/gateway

# Run all tests
test: all
	$(MAKE) -C test/access
	$(MAKE) -C test/storage
	$(MAKE) -C test/meta
	$(MAKE) -C test/cluster
	$(MAKE) -C test/gateway

# Clean all
clean:
	$(MAKE) -C src/access clean || true
	$(MAKE) -C src/storage clean || true
	$(MAKE) -C src/meta clean || true
	$(MAKE) -C src/cluster clean || true
	$(MAKE) -C src/gateway clean || true
	$(MAKE) -C test/access clean || true
	$(MAKE) -C test/storage clean || true
	$(MAKE) -C test/meta clean || true
	$(MAKE) -C test/cluster clean || true
	$(MAKE) -C test/gateway clean || true
	rm -f /tmp/*.o /tmp/test_*
