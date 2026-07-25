# Compiler
CC = gcc

# Compiler flags
COMMON_CFLAGS = -std=c11 -pedantic -Wall -Werror -Wextra -Wswitch-enum -Wunreachable-code
DEBUG_CFLAGS = $(COMMON_CFLAGS) -g -fsanitize=undefined -fsanitize=address
RELEASE_CFLAGS = $(COMMON_CFLAGS) -O3 -DNDEBUG

CFLAGS ?= $(DEBUG_CFLAGS)

# Linker flags
LDFLAGS = -llua5.4 -lm

# Directories
SRC_DIR = src
BIN_DIR = bin
TEST_DIR = tests

# Installation path
PREFIX ?= /usr/local

# Targets
TARGET = $(BIN_DIR)/neo
TEST_TARGET = $(BIN_DIR)/neo_test

# Source files
SRCS = $(shell find $(SRC_DIR) -name "*.c")
HDRS = $(shell find $(SRC_DIR) -name "*.h")
TEST_SRCS = $(wildcard $(TEST_DIR)/main.c)
TEST_DEPS = $(shell find $(TEST_DIR) -name "*.c" -o -name "*.h")

# Ensure bin/ exists
$(shell mkdir -p $(BIN_DIR))

# Main build rule
$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

# Test rule
$(TEST_TARGET): $(TEST_DEPS) $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(TEST_SRCS) $(filter-out src/main.c,$(SRCS)) $(LDFLAGS) -o $@

# Phony targets
all: $(TARGET)

build: clean $(TARGET)

release: CFLAGS = $(RELEASE_CFLAGS)
release: clean $(TARGET)

install: release
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/neo

test: clean $(TEST_TARGET)
	./$(TEST_TARGET) $(TESTFLAGS)

# Quick test loop: skips clean (usage: make testq TESTFLAGS='--filter buffer')
testq: $(TEST_TARGET)
	./$(TEST_TARGET) $(TESTFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)/* compile_commands.json

format:
	find $(SRC_DIR) -name "*.c" -o -name "*.h" | xargs clang-format -i --fallback-style=Webkit

lint: format
	clang-tidy $(SRCS) $(TEST_SRCS) -checks=-*,clang-diagnostic-*,clang-analyzer-*,-clang-analyzer-cplusplus*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling -- $(CFLAGS) $(LDFLAGS)

.PHONY: all build release install test testq run clean format lint