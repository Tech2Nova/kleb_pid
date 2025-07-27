// program_a_bpf.c

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

// 定义 map：用于发送事件到用户空间
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);  // 定义 map 类型
    __uint(max_entries, 128);                     // 最大 CPU 数
    __type(key, u32);
    __type(value, u32);
} events SEC(".maps");

// 跟踪 execve 系统调用
SEC("tracepoint/syscalls/sys_enter_execve")
int trace_execve(struct trace_event_raw_sys_enter *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;  // 获取当前进程的 PID

    // 向 perf event 输出事件
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &pid, sizeof(pid));

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
