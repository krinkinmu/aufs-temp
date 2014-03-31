CC=gcc
CFLAGS=-Wall -Wextra -Werror -ansi -pedantic

all: mkfs.c
	$(CC) $(CFLAGS) mkfs.c -o mkfs.aufs

clean:
	rm -rf mkfs.aufs

.PHONY: clean
