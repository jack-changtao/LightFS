# test/access/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk

CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I../../src/access \
          -I$(SPDK_ROOT_DIR)/include
LDFLAGS := -lllhttp -lm
LDFLAGS_SPDK := -L$(SPDK_ROOT_DIR)/build/lib -lspdk -lpthread -lllhttp -lm

SRCDIR = ../../src/access
MOCKDIR = ../mocks

UNIT_TESTS = test_sigv4 test_routes test_s3_xml test_handlers \
             test_http_parser test_http_response test_conn_state
INT_TESTS = test_http_integration

TESTS = $(UNIT_TESTS) $(INT_TESTS)

ACCESS_OBJS = $(SRCDIR)/sigv4.o $(SRCDIR)/access_routes.o \
              $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o \
              $(SRCDIR)/http_parser.o $(SRCDIR)/http_response.o \
              $(SRCDIR)/http_server.o $(SRCDIR)/access_server.o

all: $(TESTS)

test_sigv4: test_sigv4.c $(SRCDIR)/sigv4.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/sigv4.o -lm

test_routes: test_routes.c $(SRCDIR)/access_routes.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_routes.o -lm

test_s3_xml: test_s3_xml.c $(SRCDIR)/s3_xml.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/s3_xml.o -lm

test_handlers: test_handlers.c $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/access_handlers.o $(SRCDIR)/s3_xml.o $(MOCKDIR)/mock_gateway.o -lm

test_http_parser: test_http_parser.c $(SRCDIR)/http_parser.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/http_parser.o -lllhttp -lm

test_http_response: test_http_response.c $(SRCDIR)/http_response.o
	$(CC) $(CFLAGS) -o $@ $< $(SRCDIR)/http_response.o -lm

test_conn_state: test_conn_state.c $(ACCESS_OBJS)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $< $(ACCESS_OBJS) -lllhttp -lm

test_http_integration: test_http_integration.c $(ACCESS_OBJS)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $< $(ACCESS_OBJS) -lllhttp -lm

run: all
	@echo "=== Running test_sigv4 ===" && ./test_sigv4
	@echo "=== Running test_routes ===" && ./test_routes
	@echo "=== Running test_s3_xml ===" && ./test_s3_xml
	@echo "=== Running test_handlers ===" && ./test_handlers
	@echo "=== Running test_http_parser ===" && ./test_http_parser
	@echo "=== Running test_http_response ===" && ./test_http_response
	@echo "=== Running test_conn_state ===" && ./test_conn_state
	@echo "=== Running test_http_integration ===" && ./test_http_integration

run_unit: $(UNIT_TESTS)
	@echo "=== Running test_sigv4 ===" && ./test_sigv4
	@echo "=== Running test_routes ===" && ./test_routes
	@echo "=== Running test_s3_xml ===" && ./test_s3_xml
	@echo "=== Running test_handlers ===" && ./test_handlers
	@echo "=== Running test_http_parser ===" && ./test_http_parser
	@echo "=== Running test_http_response ===" && ./test_http_response
	@echo "=== Running test_conn_state ===" && ./test_conn_state

clean:
	rm -f $(TESTS)

.PHONY: all run run_unit clean
