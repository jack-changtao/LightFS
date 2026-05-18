# src/access/Makefile
CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include -I/root/spdk/include
LDFLAGS := -L/root/spdk/build/lib -lspdk -lpthread -lllhttp

SRCS = sigv4.c access_routes.c access_handlers.c s3_xml.c \
       http_parser.c http_response.c http_server.c access_server.c
OBJS = $(SRCS:.c=.o)

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)

.PHONY: all clean
