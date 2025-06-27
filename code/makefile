# Compiler and tools
CC = gcc
CLANG = clang
BPFTOOL = bpftool

# Source files
MAIN_SRC = the_main.c
COLLECT_SRC = collect.c
RECEIVE_SRC = receive.c
BPF_SRC = program_a_bpf.c

# Header files
HEADERS = collect.h common.h

# Object files
MAIN_OBJ = $(MAIN_SRC:.c=.o)
COLLECT_OBJ = $(COLLECT_SRC:.c=.o)
RECEIVE_OBJ = $(RECEIVE_SRC:.c=.o)
BPF_OBJ = $(BPF_SRC:.c=.o)
SKEL_H = program_a_bpf.skel.h

# Output binary
TARGET = the_main

# Check for pkg-config and set flags
PKG_CONFIG := $(shell command -v pkg-config 2>/dev/null)
ifdef PKG_CONFIG
    CFLAGS = -g -O2 $(shell pkg-config --cflags libbpf)
    LDFLAGS = $(shell pkg-config --libs libbpf) -lpthread
else
    # Fallback paths for libbpf (adjust these based on your system)
    CFLAGS = -g -O2 -I/usr/include
    LDFLAGS = -L/usr/lib/x86_64-linux-gnu -lbpf -lpthread
endif

# BPF compiler flags
BPF_CFLAGS = -g -O2 -target bpf

# Default target
all: $(TARGET)

# Link the final binary
$(TARGET): $(MAIN_OBJ) $(COLLECT_OBJ) $(RECEIVE_OBJ) $(SKEL_H)
	$(CC) -o $@ $(MAIN_OBJ) $(COLLECT_OBJ) $(RECEIVE_OBJ) $(LDFLAGS)

# Compile C source files
$(MAIN_OBJ): $(MAIN_SRC) $(SKEL_H) $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(RECEIVE_OBJ): $(RECEIVE_SRC) $(SKEL_H) $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(COLLECT_OBJ): $(COLLECT_SRC) $(SKEL_H) $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Generate BPF skeleton header
$(SKEL_H): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $(BPF_OBJ) > $@

# Compile BPF program
$(BPF_OBJ): $(BPF_SRC)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# Clean up generated files
clean:
	rm -f $(TARGET) $(MAIN_OBJ) $(COLLECT_OBJ) $(RECEIVE_OBJ) $(BPF_OBJ) $(SKEL_H)

# Phony targets
.PHONY: all clean
