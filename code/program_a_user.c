/*program_a_user.c*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <time.h>
#include <linux/types.h>
#include "program_a_bpf.skel.h"

static volatile sig_atomic_t exiting = 0;
struct perf_buffer *pb = NULL;
static FILE *log_file = NULL;

// PID 风暴检测参数
#define EVENT_THRESHOLD 10
#define TIME_WINDOW_NS 1000000L // 1ms（纳秒）
#define PID_IGNORE_WINDOW_NS 5000000000L // 5秒（纳秒）
static int event_count = 0;
static long last_ns = 0;

void handle_signal(int sig) {
    exiting = 1;
}

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // 清理僵尸进程
    }
}

static void handle_event(void *ctx, int cpu, void *data, __u32 data_sz) {
    if (data_sz < sizeof(uint32_t)) {
        fprintf(stderr, "无效的事件大小: %u\n", data_sz);
        return;
    }

    uint32_t pid = *(uint32_t *)data;
    struct program_a_bpf *skel = (struct program_a_bpf *)ctx;

    // 检查 processed_pids map，过滤 5 秒内的重复 PID
    __u64 last_time;
    int err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.processed_pids), &pid, &last_time);
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "获取时间失败: %s\n", strerror(errno));
        return;
    }
    long curr_ns = ts.tv_sec * 1000000000L + ts.tv_nsec;
    if (err == 0 && (curr_ns - last_time) < PID_IGNORE_WINDOW_NS) {
        return; // 忽略 5秒内的重复 PID
    }
    // 更新 processed_pids map
    err = bpf_map_update_elem(bpf_map__fd(skel->maps.processed_pids), &pid, &curr_ns, BPF_ANY);
    if (err) {
        fprintf(stderr, "更新 processed_pids map 失败，PID %u: %s\n", pid, strerror(-err));
    }

    // PID 风暴检测
    if ((curr_ns - last_ns) >= TIME_WINDOW_NS) {
        event_count = 0;
        last_ns = curr_ns;
    }
    event_count++;
    if (event_count > EVENT_THRESHOLD) {
        fprintf(stderr, "PID storm detected！( %d events in 1 ms)\n", event_count);
        exiting = 1;
        return;
    }

    // 记录到日志文件
    if (log_file) {
        int ret = fprintf(log_file, "[%ld.%09ld] PID: %u\n", ts.tv_sec, ts.tv_nsec, pid);
        if (ret < 0) {
            fprintf(stderr, "写入日志文件失败: %s\n", strerror(errno));
        }
        if (fflush(log_file) != 0) {
            fprintf(stderr, "刷新日志文件失败: %s\n", strerror(errno));
        }
    } else {
        fprintf(stderr, "日志文件未打开！\n");
    }

    printf("[execve] caught process PID: %u\n", pid);

    // 限制子进程数量
    static int active_children = 0;
    #define MAX_CHILDREN 5
    if (active_children >= MAX_CHILDREN) {
        fprintf(stderr, "子进程数量过多，等待...\n");
        wait(NULL);
        active_children--;
    }

    // 调用 program_b
    pid_t child_pid = fork();
    if (child_pid == -1) {
        fprintf(stderr, "fork 失败: %s\n", strerror(errno));
        return;
    } else if (child_pid == 0) {
        // 重定向 program_b 的输出到 txt 文件
        FILE *fp;
        fp = freopen("./program_b_out.txt", "w", stdout);
        if (!fp) {
            fprintf(stderr, "重定向 stdout 到 ./program_b_out.txt 失败: %s\n", strerror(errno));
        }
        if (fp) fflush(fp); // 强制刷新缓冲区
        char pid_str[17];
        snprintf(pid_str, sizeof(pid_str), "%u", pid);
        execl("./program_b", "program_b", pid_str, (char *)NULL);
        fprintf(stderr, "execl 失败，./program_b: %s\n", strerror(errno));
        _exit(1);
    } else {
        active_children++;
    }

    // 将 program_b 的子进程 PID 添加到 exclude_pids
    __u8 val = 1;
    err = bpf_map_update_elem(bpf_map__fd(skel->maps.exclude_pids), &child_pid, &val, BPF_ANY);
    if (err) {
        fprintf(stderr, "更新 exclude_pids map 失败，子进程 PID %d: %s\n", child_pid, strerror(-err));
    }
}

int main() {
    struct program_a_bpf *skel;
    int err;

    // 打开日志文件
    log_file = fopen("./execve_log.txt", "w");
    if (!log_file) {
        fprintf(stderr, "打开日志文件失败: %s\n", strerror(errno));
        return 1;
    }
    if (fprintf(log_file, "开始记录 execve 日志\n") < 0) {
        fprintf(stderr, "写入初始日志失败: %s\n", strerror(errno));
        fclose(log_file);
        return 1;
    }
    if (fflush(log_file) != 0) {
        fprintf(stderr, "刷新初始日志失败: %s\n", strerror(errno));
        fclose(log_file);
        return 1;
    }

    // 提高资源限制
    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &rlim);
    rlim.rlim_cur = 1024;
    rlim.rlim_max = 1024;
    setrlimit(RLIMIT_NPROC, &rlim);

    skel = program_a_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "打开并加载 BPF 框架失败\n");
        fclose(log_file);
        return 1;
    }

    err = program_a_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "附加 BPF 程序失败: %s\n", strerror(-err));
        program_a_bpf__destroy(skel);
        fclose(log_file);
        return 1;
    }

    // 清空 exclude_pids map
    int map_fd = bpf_map__fd(skel->maps.exclude_pids);
    if (map_fd < 0) {
        fprintf(stderr, "获取 exclude_pids map 文件描述符失败\n");
        program_a_bpf__destroy(skel);
        fclose(log_file);
        return 1;
    }
    __u32 next_key;
    int clear_err = 0;
    while (bpf_map_get_next_key(map_fd, NULL, &next_key) == 0) {
        if (bpf_map_delete_elem(map_fd, &next_key) != 0) {
            fprintf(stderr, "删除 exclude_pids map 条目 %u 失败: %s\n", next_key, strerror(errno));
            clear_err = 1;
        }
    }
    if (clear_err) {
        fprintf(stderr, "清空 exclude_pids map 过程中发生错误\n");
    } else {
        fprintf(stderr, "clear exclude_pids map\n");
    }

    // 将当前进程 PID 添加到 exclude_pids
    __u32 pid = getpid();
    __u8 val = 1;
    err = bpf_map_update_elem(bpf_map__fd(skel->maps.exclude_pids), &pid, &val, BPF_ANY);
    if (err) {
        fprintf(stderr, "更新 exclude_pids map 失败，PID %d: %s\n", pid, strerror(-err));
    }

    // 配置 perf buffer
    struct perf_buffer_opts opts = {
        .sample_cb = handle_event,
        .ctx = skel,
    };
    pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 64, &opts);
    if (!pb) {
        fprintf(stderr, "创建 perf buffer 失败: %s\n", strerror(errno));
        program_a_bpf__destroy(skel);
        fclose(log_file);
        return 1;
    }

    printf("program A is running. press Ctrl+C stop...\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, handle_sigchld);

    while (!exiting) {
        err = perf_buffer__poll(pb, 100);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "轮询 perf buffer 出错: %d\n", err);
            break;
        }
    }

    perf_buffer__free(pb);
    program_a_bpf__destroy(skel);
    if (log_file) {
        fclose(log_file);
    }
    printf("退出。\n");
    return 0;
}
