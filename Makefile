CC = gcc
CFLAGS = -Wall -Wextra -O2

server: server.c messages.h lonesha256.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread server.c -o server $(LDFLAGS) $(LDLIBS)

.PHONY: clean
clean:
	rm -f server
