CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread -D_POSIX_C_SOURCE=200112L
CPPFLAGS := -Iinclude
LDFLAGS := -pthread

BIN_DIR := bin
COMMON_SRC := src/protocol.c src/storage.c
SERVER_SRC := src/server.c src/user.c src/chat.c src/file_transfer.c src/bbs.c src/social.c $(COMMON_SRC)
CLIENT_SRC := src/client.c src/file_transfer.c src/user.c $(COMMON_SRC)
SERVER_BIN := $(BIN_DIR)/server
CLIENT_BIN := $(BIN_DIR)/client
PREFIX ?= /usr/local

.PHONY: all clean test install uninstall

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BIN_DIR):
	mkdir -p $@

$(SERVER_BIN): $(SERVER_SRC) include/server.h include/protocol.h include/user.h include/chat.h include/file_transfer.h | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERVER_SRC) -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC) include/client.h include/protocol.h include/file_transfer.h include/user.h | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CLIENT_SRC) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)

test: all
	python3 tests/test_multiclient.py
	python3 tests/test_restart.py
	python3 tests/test_storage_features.py
	python3 tests/test_group_visibility_bridge.py
	python3 tests/test_group_create_edges.py
	python3 tests/test_group_message_permissions.py
	python3 tests/test_qt_bbs_protocol.py
	python3 tests/test_web_gateway.py

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(SERVER_BIN) $(DESTDIR)$(PREFIX)/bin/bbs-server
	install -m 0755 $(CLIENT_BIN) $(DESTDIR)$(PREFIX)/bin/bbs-client

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/bbs-server
	rm -f $(DESTDIR)$(PREFIX)/bin/bbs-client
