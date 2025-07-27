# 定义编译器和工具
CC = gcc
CLANG = clang
BPFTOOL = bpftool

# 编译标志
CFLAGS = -g -Wall -O2 -D_GNU_SOURCE
BPF_CFLAGS = -g -O2 -target bpf
LDFLAGS = -lbpf -pthread

# 目标可执行文件
TARGET = collect

# 源文件和目标文件
C_SOURCES = collect.c perf_monitor.c the_main.c
C_OBJECTS = $(C_SOURCES:.c=.o)
BPF_SOURCE = program_a_bpf.c
BPF_OBJECT = program_a_bpf.o

# 默认目标
all: $(TARGET)

# 生成可执行文件
$(TARGET): $(C_OBJECTS) $(BPF_OBJECT)
	$(CC) $(C_OBJECTS) $(LDFLAGS) -o $(TARGET)

# 编译 C 文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译 BPF 程序
$(BPF_OBJECT): $(BPF_SOURCE)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(BPFTOOL) gen skeleton $(BPF_OBJECT) > program_a_bpf.skel.h

# 清理生成的文件
clean:
	rm -f $(TARGET) $(C_OBJECTS) $(BPF_OBJECT) program_a_bpf.skel.h

# 声明伪目标
.PHONY: all clean
