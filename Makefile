# Makefile for Election System Project
# Supports both Windows and Linux

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2 -finput-charset=utf-8 -fexec-charset=utf-8
INCLUDES = -Iinclude

# Platform detection
ifeq ($(OS),Windows_NT)
    PLATFORM = Windows
    LDFLAGS = -lws2_32 -lpthread -lwininet
    EXECUTABLE_EXT = .exe
    RM = del /Q
    MKDIR = mkdir
    CP = copy
else
    PLATFORM = Linux
    LDFLAGS = -lpthread -lcurl
    EXECUTABLE_EXT = 
    RM = rm -f
    MKDIR = mkdir -p
    CP = cp
endif

# Directory structure
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
DATA_DIR = data
COMMON_DIR = $(SRC_DIR)/common
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client

# Output executables
SERVER_TARGET = $(BUILD_DIR)/server$(EXECUTABLE_EXT)
CLIENT_TARGET = $(BUILD_DIR)/client$(EXECUTABLE_EXT)

# Source files
COMMON_SOURCES = $(wildcard $(COMMON_DIR)/*.c)
SERVER_SOURCES = $(wildcard $(SERVER_DIR)/*.c)
CLIENT_SOURCES = $(wildcard $(CLIENT_DIR)/*.c)

# Object files
COMMON_OBJECTS = $(COMMON_SOURCES:$(COMMON_DIR)/%.c=$(BUILD_DIR)/common_%.o)
SERVER_OBJECTS = $(SERVER_SOURCES:$(SERVER_DIR)/%.c=$(BUILD_DIR)/server_%.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:$(CLIENT_DIR)/%.c=$(BUILD_DIR)/client_%.o)

# Default target
all: directories $(SERVER_TARGET) $(CLIENT_TARGET)

# Create necessary directories
directories:
	@echo "Creating directories for $(PLATFORM)..."
ifeq ($(PLATFORM),Windows)
	@if not exist "$(BUILD_DIR)" $(MKDIR) "$(BUILD_DIR)"
	@if not exist "$(DATA_DIR)" $(MKDIR) "$(DATA_DIR)"
else
	@$(MKDIR) $(BUILD_DIR) $(DATA_DIR)
endif

# Build server
$(SERVER_TARGET): $(COMMON_OBJECTS) $(SERVER_OBJECTS)
	@echo "Building server for $(PLATFORM)..."
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	@echo "Server built successfully: $@"

# Build client
$(CLIENT_TARGET): $(COMMON_OBJECTS) $(CLIENT_OBJECTS)
	@echo "Building client for $(PLATFORM)..."
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
	@echo "Client built successfully: $@"

# Compile common source files
$(BUILD_DIR)/common_%.o: $(COMMON_DIR)/%.c
	@echo "Compiling common module: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile server source files
$(BUILD_DIR)/server_%.o: $(SERVER_DIR)/%.c
	@echo "Compiling server module: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile client source files
$(BUILD_DIR)/client_%.o: $(CLIENT_DIR)/%.c
	@echo "Compiling client module: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Server only target
server: directories $(SERVER_TARGET)

# Client only target
client: directories $(CLIENT_TARGET)

# Debug build
debug: CFLAGS += -DDEBUG -g3
debug: all

# Release build
release: CFLAGS += -DNDEBUG -O3
release: clean all

# Install cJSON library (for development)
install-deps:
ifeq ($(PLATFORM),Windows)
	@echo "Please install cJSON library manually for Windows"
	@echo "Download from: https://github.com/DaveGamble/cJSON"
else
	@echo "Installing dependencies for Linux..."
	@sudo apt-get update
	@sudo apt-get install libcjson-dev libcurl4-openssl-dev
endif

# Create sample data files
sample-data:
	@echo "Creating sample data files..."
ifeq ($(PLATFORM),Windows)
	@if not exist "$(DATA_DIR)" $(MKDIR) "$(DATA_DIR)"
else
	@$(MKDIR) $(DATA_DIR)
endif
	@echo "admin:5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8" > $(DATA_DIR)/users.txt
	@echo "user1:ef92b778bafe771e89245b89ecbc08a44a4e166c06659911881f383d4473e94f" >> $(DATA_DIR)/users.txt
	@echo "Sample user data created (admin/admin, user1/password)"

# Run server
run-server: $(SERVER_TARGET)
	@echo "Starting server..."
	./$(SERVER_TARGET)

# Run client
run-client: $(CLIENT_TARGET)
	@echo "Starting client..."
	./$(CLIENT_TARGET)

# Clean build files
clean:
	@echo "Cleaning build files..."
ifeq ($(PLATFORM),Windows)
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
else
	@$(RM) $(BUILD_DIR)/*.o $(SERVER_TARGET) $(CLIENT_TARGET)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
endif
	@echo "Clean completed"

# Clean all generated files including data
clean-all: clean
	@echo "Cleaning all generated files..."
ifeq ($(PLATFORM),Windows)
	@if exist "$(DATA_DIR)" rmdir /S /Q "$(DATA_DIR)"
else
	@$(RM) -r $(DATA_DIR)
endif

# Show help
help:
	@echo "Available targets:"
	@echo "  all         - Build both server and client"
	@echo "  server      - Build server only"
	@echo "  client      - Build client only"
	@echo "  debug       - Build with debug flags"
	@echo "  release     - Build optimized release version"
	@echo "  install-deps - Install required dependencies"
	@echo "  sample-data - Create sample data files"
	@echo "  run-server  - Build and run server"
	@echo "  run-client  - Build and run client"
	@echo "  clean       - Remove build files"
	@echo "  clean-all   - Remove all generated files"
	@echo "  help        - Show this help message"

# Phony targets
.PHONY: all directories server client debug release install-deps sample-data run-server run-client clean clean-all help 