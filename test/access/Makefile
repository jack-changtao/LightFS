# test/access/Makefile
CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I../../src/access
LDFLAGS := -lm

SRCDIR = ../../src/access
MOCKDIR = ../mocks

TESTS = test_sigv4 test_routes test_s3_xml test_handlers

all: $(TESTS)

test_sigv4: test_sigv4.c $(SRCDIR)/sigv4.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/sigv4.o $(LDFLAGS)

test_routes: test_routes.c $(SRCDIR)/access_routes.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_routes.o $(LDFLAGS)

test_s3_xml: test_s3_xml.c $(SRCDIR)/s3_xml.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/s3_xml.o $(LDFLAGS)

test_handlers: test_handlers.c $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o $(LDFLAGS)

run: all
	@echo "=== Running test_sigv4 ==="
	./test_sigv4
	@echo "=== Running test_routes ==="
	./test_routes
	@echo "=== Running test_s3_xml ==="
	./test_s3_xml
	@echo "=== Running test_handlers ==="
	./test_handlers

clean:
	rm -f $(TESTS)

.PHONY: all run clean
