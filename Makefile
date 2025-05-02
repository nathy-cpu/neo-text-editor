# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -std=c11 -pedantic -Wall -Werror -Wextra -ggdb -Wswitch-enum -Wunreachable-code -fsanitize=undefined -fsanitize=address -I/usr/local/include

# Linker flags
LDFLAGS = -llua5.4 -lm

# Directories
SRC_DIR = src
BIN_DIR = bin

# Executable name
TARGET  = $(BIN_DIR)/ted


# Auto-detect all .c files in src/
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BIN_DIR)/%.o, $(SOURCES))

# Ensure bin/ exists
$(shell mkdir -p $(BIN_DIR))

# Main build rule
all: build

build: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Phony targets
run: build
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)/*

format:
	find $(SRC_DIR) -name "*.c" -o -name "*.h" | xargs clang-format

lint:
	clang-tidy $(SOURCES) -- $(CFLAGS)

.PHONY: all build run clean format lint
