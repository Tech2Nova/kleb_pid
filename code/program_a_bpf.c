/* program_a_bpf.c */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "program_a.h"

// 定义排除 PID 的映射
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);   // PID
    __type(value, u8);  // Dummy value
} exclude_pids SEC(".maps");

// 定义发送事件到用户空间的映射
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(max_entries, 128);
    __type(key, u32);
    __type(value, u32);
} events SEC(".maps");

// 新增 processed_pids 映射，用于记录已处理的 PID 和时间戳
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);   // PID
    __type(value, u64); // 时间戳（纳秒）
} processed_pids SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int trace_execve(struct trace_event_raw_sys_enter *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;

    // 检查 PID 是否在排除列表中
    u8 *val = bpf_map_lookup_elem(&exclude_pids, &pid);
    if (val) {
        return 0; // 跳过排除的 PID
    }

    // 获取程序名
    struct event ev = {0};
    ev.pid = pid;
    bpf_get_current_comm(&ev.comm, sizeof(ev.comm));

    // 发送事件到用户空间
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &ev, sizeof(ev));
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
