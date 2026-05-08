# LightFS top-level Makefile
# Phase 1: Storage Engine, Phase 2: Access Layer

.PHONY: all clean test access storage

# Default target
all: access storage

# Build Access layer
access:
	$(MAKE) -C src/access

# Build Storage engine
storage:
	$(MAKE) -C src/storage

# Run all tests
test: access storage
	$(MAKE) -C test/access
	$(MAKE) -C test/storage

# Clean all
clean:
	$(MAKE) -C src/access clean || true
	$(MAKE) -C src/storage clean || true
	$(MAKE) -C test/access clean || true
	$(MAKE) -C test/storage clean || true
	rm -f /tmp/*.o /tmp/test_*
