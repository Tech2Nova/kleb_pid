# Makefile for compiling BPF and user-space programs

CC = gcc
CLANG = clang
BPFTOOL = bpftool
CFLAGS = -g -Wall -O2
BPF_CFLAGS = -g -Wall -O2 -target bpf -I/usr/include -I/usr/include/$(shell uname -m)-linux-gnu -I/usr/include/bpf
LDFLAGS = -lbpf -pthread

# Directories
SRC_DIR = .
BUILD_DIR = build
BPF_OBJ = $(BUILD_DIR)/program_a_bpf.o
BPF_SKEL = $(BUILD_DIR)/program_a_bpf.skel.h
USER_OBJ = $(BUILD_DIR)/program_a_user.o
PROG_B_OBJ = $(BUILD_DIR)/program_b.o
TARGETS = $(BUILD_DIR)/program_a_user $(BUILD_DIR)/program_b

# Ensure build directory exists
$(shell mkdir -p $(BUILD_DIR))

# Default target
all: $(TARGETS)

# Compile BPF program to object file
$(BPF_OBJ): $(SRC_DIR)/program_a_bpf.c
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# Generate BPF skeleton header
$(BPF_SKEL): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

# Compile program_a_user.c
$(USER_OBJ): $(SRC_DIR)/program_a_user.c $(BPF_SKEL)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -c $< -o $@

# Link program_a_user
$(BUILD_DIR)/program_a_user: $(USER_OBJ)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Compile and link program_b.c
$(BUILD_DIR)/program_b: $(SRC_DIR)/program_b.c
	$(CC) $(CFLAGS) -c $< -o $(PROG_B_OBJ)
	$(CC) $(CFLAGS) $(PROG_B_OBJ) -o $@ $(LDFLAGS)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Phony targets
.PHONY: all clean
