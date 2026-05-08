# src/access/Makefile
CC ?= gcc
CFLAGS := -Wall -Wextra -std=c11 -g -I../../include

SRCS = sigv4.c access_routes.c access_handlers.c s3_xml.c access_server.c
OBJS = $(SRCS:.c=.o)

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)

.PHONY: all clean
