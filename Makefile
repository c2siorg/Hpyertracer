# QEMU Hypertracer Plugin Makefile

# Plugin name
PLUGIN_NAME = hypertracer

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# QEMU installation paths (adjust these for your system)
QEMU_CFLAGS = $(shell pkg-config --cflags glib-2.0)
QEMU_LIBS = $(shell pkg-config --libs glib-2.0)

# Detect QEMU installation
QEMU_PREFIX ?= /usr/local
QEMU_INCLUDE = $(QEMU_PREFIX)/include/qemu
QEMU_LIB = $(QEMU_PREFIX)/lib

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -fPIC -shared
CFLAGS += -I$(INCLUDE_DIR) -I$(QEMU_INCLUDE)
CFLAGS += $(QEMU_CFLAGS)
CFLAGS += -DQEMU_PLUGIN

# Libraries
LIBS = $(QEMU_LIBS)

# Source files
SOURCES = $(SRC_DIR)/$(PLUGIN_NAME).c
OBJECTS = $(BUILD_DIR)/$(PLUGIN_NAME).o
TARGET = $(BUILD_DIR)/lib$(PLUGIN_NAME).so

# Default target
all: $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link the plugin
$(TARGET): $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LIBS)

# Install target (optional)
install: $(TARGET)
	@echo "Installing plugin to $(QEMU_LIB)/qemu-plugins/"
	mkdir -p $(QEMU_LIB)/qemu-plugins
	cp $(TARGET) $(QEMU_LIB)/qemu-plugins/

# Clean build files
clean:
	rm -rf $(BUILD_DIR)
	rm -f hypertracer_output.txt hypertracer_report.html

# Development build with debug symbols
debug: CFLAGS += -DDEBUG -ggdb3
debug: $(TARGET)

# Help target
help:
	@echo "Available targets:"
	@echo "  all      - Build the plugin (default)"
	@echo "  debug    - Build with debug symbols"
	@echo "  install  - Install the plugin to QEMU plugins directory"
	@echo "  clean    - Remove build files and output"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Usage example:"
	@echo "  make"
	@echo "  qemu-system-x86_64 -plugin ./build/libhypertracer.so [other options]"

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists glib-2.0 || (echo "ERROR: glib-2.0 not found" && exit 1)
	@echo "Dependencies OK"

.PHONY: all clean debug install help check-deps
