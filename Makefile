# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -std=c11 -pedantic -Wall -Werror -Wextra -Wswitch-enum -Wunreachable-code -fsanitize=undefined -fsanitize=address

# Linker flags
LDFLAGS = -llua5.4 -lm

# Directories
SRC_DIR = src
BIN_DIR = bin
TEST_DIR = tests

# Targets
TARGET = $(BIN_DIR)/neo
TEST_TARGET = $(BIN_DIR)/file_io_test

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)

# Ensure bin/ exists
$(shell mkdir -p $(BIN_DIR))

# Main build rule
$(TARGET):
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@

# Test rule
$(TEST_TARGET):
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_SRCS) $(SRCS) -o $@

# Phony targets
all: $(TARGET)

build: $(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)/* compile_commands.json

format:
	find $(SRC_DIR) -name "*.c" -o -name "*.h" | xargs clang-format -i --fallback-style=Webkit

lint:
	clang-tidy $(SRCS) $(TEST_SRCS) -checks=-*,clang-analyzer-*,-clang-analyzer-cplusplus* -- $(CFLAGS) $(LDFLAGS)

.PHONY: all build test run clean format lint
