/* program_a.h */
#ifndef PROGRAM_A_H
#define PROGRAM_A_H

typedef unsigned int u32; // 与 vmlinux.h 和用户空间兼容

struct event {
    u32 pid;
    char comm[16]; // 匹配 Linux TASK_COMM_LEN
};

#endif /* PROGRAM_A_H */
