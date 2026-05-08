# LightFS top-level Makefile
# Phase 2: Access Layer build

.PHONY: all clean test access

# Default target
all: access

# Build Access layer
access:
	$(MAKE) -C src/access

# Run Access layer tests
test: access
	$(MAKE) -C test/access

# Clean all
clean:
	$(MAKE) -C src/access clean || true
	$(MAKE) -C test/access clean || true
	rm -f /tmp/*.o /tmp/test_*
