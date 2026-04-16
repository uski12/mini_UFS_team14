# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -g -Iinclude -DFUSE_USE_VERSION=31

# FUSE flags (auto-detected via pkg-config)
FUSE_CFLAGS = $(shell pkg-config fuse3 --cflags)
FUSE_LIBS   = $(shell pkg-config fuse3 --libs)

# Source files
SRC = main.c path_utils.c cow.c ops_read.c ops_write.c

# Object files
OBJ = $(SRC:.c=.o)

# Output binary
TARGET = mini_unionfs

# Default target
all: $(TARGET)

# Link step
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(FUSE_LIBS)

# Compile step
%.o: %.c
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -c $< -o $@

# Clean build files
clean_obj:
	rm -f $(OBJ)

clean_all:
	rm -f $(OBJ) $(TARGET)



# Run example (optional helper)
run: $(TARGET)
	./$(TARGET) lower upper mount -f
