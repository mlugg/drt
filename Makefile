.PHONY: all clean

CC=clang

CFLAGS=-Wall -Werror

all: drt

clean:
	rm -f drt

drt:
	$(CC) $(CFLAGS) main.c -o $@
