CC = gcc
CFLAGS = -Wall -Wextra -O2

server: server.c
	$(CC) $(CFLAGS) server.c -o server

clean:
	rm -f server