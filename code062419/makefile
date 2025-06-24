# Makefile for program_a_bpf, program_a_user, and program_b

# 编译器和工具
CC = gcc
BPF_CC = clang
BPFTOOL = bpftool
LIBBPF = -lbpf

# 编译标志
CFLAGS = -Wall -O2 -g
BPF_CFLAGS = -target bpf -Wall -O2 -g -D__KERNEL__ -D__BPF_TRACING__ -D__TARGET_ARCH_x86_64
LDFLAGS = $(LIBBPF)

# 输出文件
PROGRAM_A_BPF_OBJ = program_a_bpf.o
PROGRAM_A_BPF_SKEL = program_a_bpf.skel.h
PROGRAM_A_USER = program_a_user
PROGRAM_B = program_b

# 源文件和头文件
PROGRAM_A_BPF_SRC = program_a_bpf.c
PROGRAM_A_USER_SRC = program_a_user.c
PROGRAM_B_SRC = program_b.c
PROGRAM_A_HEADER = program_a.h

# 默认目标
all: $(PROGRAM_A_USER) $(PROGRAM_B)

# 编译 eBPF 程序
$(PROGRAM_A_BPF_OBJ): $(PROGRAM_A_BPF_SRC) $(PROGRAM_A_HEADER)
	$(BPF_CC) $(BPF_CFLAGS) -I. -c $< -o $@

# 生成 eBPF skeleton 文件
$(PROGRAM_A_BPF_SKEL): $(PROGRAM_A_BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

# 编译 program_a_user
$(PROGRAM_A_USER): $(PROGRAM_A_USER_SRC) $(PROGRAM_A_BPF_SKEL) $(PROGRAM_A_HEADER)
	$(CC) $(CFLAGS) -I. $< $(LDFLAGS) -o $@

# 编译 program_b
$(PROGRAM_B): $(PROGRAM_B_SRC)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

# 清理
clean:
	@rm -f $(PROGRAM_A_BPF_OBJ) $(PROGRAM_A_BPF_SKEL) $(PROGRAM_A_USER) $(PROGRAM_B)

# 运行 program_a_user（需要 root 权限）
run: $(PROGRAM_A_USER) $(PROGRAM_B)
	sudo ./$(PROGRAM_A_USER) 2> debug.log

# 安装依赖（Ubuntu/Debian）
install-deps:
	sudo apt-get update
	sudo apt-get install -y clang llvm libbpf-dev bpftool linux-tools-common linux-tools-$(shell uname -r) libelf-dev

# 调试信息
info:
	@echo "Kernel version: $$(uname -r)"
	@echo "bpftool version: $$($(BPFTOOL) version)"
	@echo "libbpf version: $$(pkg-config --modversion libbpf)"
	@echo "clang version: $$($(BPF_CC) --version)"

.PHONY: all clean run install-deps info
