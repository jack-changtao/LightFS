CC ?= gcc
CFLAGS ?= -Wall -Wextra -g -I../../include -I$(SPDK_ROOT_DIR)/include
LDFLAGS ?=

SRC = segment.c journal.c cow_btree.c bs.c gc.c
OBJ = $(SRC:.c=.o)
TARGET = libstorage.a

all: $(TARGET)

$(TARGET): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
