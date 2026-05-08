# LightFS top-level Makefile
# Phase 1: Storage Engine, Phase 2: Access Layer, Phase 3: Meta Server

.PHONY: all clean test access storage meta

# Default target
all: access storage meta

# Build Access layer
access:
	$(MAKE) -C src/access

# Build Storage engine
storage:
	$(MAKE) -C src/storage

# Build Meta Server
meta:
	$(MAKE) -C src/meta

# Run all tests
test: access storage meta
	$(MAKE) -C test/access
	$(MAKE) -C test/storage
	$(MAKE) -C test/meta

# Clean all
clean:
	$(MAKE) -C src/access clean || true
	$(MAKE) -C src/storage clean || true
	$(MAKE) -C src/meta clean || true
	$(MAKE) -C test/access clean || true
	$(MAKE) -C test/storage clean || true
	$(MAKE) -C test/meta clean || true
	rm -f /tmp/*.o /tmp/test_*
