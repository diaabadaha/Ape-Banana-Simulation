# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g
LDFLAGS = -lm -lGL -lGLU -lglut -lpthread

# Directories
SRC_DIR = src
INCLUDE_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Source file
SOURCES = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/menu_parser.c \
	$(SRC_DIR)/menu_handler.c \
	$(SRC_DIR)/maze.c \
	$(SRC_DIR)/sim_state.c \
	$(SRC_DIR)/sync.c \
	$(SRC_DIR)/female_ape.c \
	$(SRC_DIR)/male_ape.c \
	$(SRC_DIR)/baby_ape.c \
	$(SRC_DIR)/fight.c \
	$(SRC_DIR)/utils.c \
	$(SRC_DIR)/simulation_threading.c \
	$(SRC_DIR)/graphics.c \
	$(SRC_DIR)/gui.c

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Target executable
TARGET = $(BIN_DIR)/ape_simulation

# Default target
all: directories $(TARGET)

# Create directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p config
	@mkdir -p results

# Link object files
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Rebuild
rebuild: clean all

# Debug build
debug: CFLAGS += -DDEBUG -g -O0
debug: rebuild

# Run the program
run: all
	./$(TARGET)

# Install OpenGL dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y build-essential freeglut3-dev libglu1-mesa-dev mesa-common-dev

.PHONY: all clean rebuild debug run directories install-deps