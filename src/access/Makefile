# src/access/Makefile
SPDK_ROOT_DIR ?= $(HOME)/spdk

CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I$(SPDK_ROOT_DIR)/include
LDFLAGS := -L$(SPDK_ROOT_DIR)/build/lib -lspdk -lpthread -lllhttp

SRCS = sigv4.c access_routes.c access_handlers.c s3_xml.c \
       http_parser.c http_response.c http_server.c access_server.c
OBJS = $(SRCS:.c=.o)

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)

.PHONY: all clean
