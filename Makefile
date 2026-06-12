CC=gcc
CFLAGS=-Wall -Wextra -g -pthread

.PHONY: all clean install uninstall run-server run-client

all: server client

server: src/server.c
	$(CC) $(CFLAGS) src/server.c -o server

client: src/client.c
	$(CC) $(CFLAGS) src/client.c -o client

install: all
	mkdir -p bin data logs uploads/bbs downloads
	cp server client bin/
	@echo "install finished: ./bin/server and ./bin/client"

run-server: server
	./server

run-client: client
	./client

clean:
	rm -f server client *.o src/*.o

uninstall:
	rm -rf bin
	rm -f server client
	@echo "uninstall finished. data/ and uploads/ are kept."
