CC = gcc
CFLAGS = -Wall -Wextra -O2

server: server.c messages.h
	$(CC) $(CFLAGS) server.c -o server

.PHONY: clean
clean:
	rm -f server
