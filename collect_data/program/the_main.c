#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "program_a_bpf.skel.h"
#include "collect.h"

// 线程工作数据
struct thread_arg {
    int pid;
    const char **events;
    char *sample_dir; // 新增样本子目录路径
};

void *monitor_thread(void *arg) {
    struct thread_arg *targ = arg;
    collect_perf_events(targ->pid, targ->events, targ->sample_dir);
    free(targ->sample_dir);
    free(targ);
    return NULL;
}

// BPF 和性能事件处理
static volatile sig_atomic_t exiting = 0;

void handle_signal(int sig) {
    exiting = 1;
}

static void handle_event(void *ctx, int cpu, void *data, __u32 data_sz) {
    if (data_sz < sizeof(uint32_t)) return;
    uint32_t pid = *(uint32_t *)data;
    printf("[execve] 捕获进程 PID: %d\n", pid);

    pthread_t tid;
    struct thread_arg *targ = malloc(sizeof(*targ));
    if (!targ) {
        perror("malloc");
        return;
    }
    targ->pid = pid;
    targ->events = NULL;
    targ->sample_dir = strdup((char *)ctx); // 从 ctx 获取样本子目录
    if (!targ->sample_dir) {
        perror("strdup");
        free(targ);
        return;
    }
    if (pthread_create(&tid, NULL, monitor_thread, targ) != 0) {
        perror("pthread_create");
        free(targ->sample_dir);
        free(targ);
    } else {
        pthread_detach(tid);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "用法: %s <样本子目录>\n", argv[0]);
        return 1;
    }

    struct program_a_bpf *skel;
    int err;

    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &rlim);

    skel = program_a_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "无法打开和加载 BPF 框架\n");
        return 1;
    }
    err = program_a_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "无法附加 BPF 程序\n");
        program_a_bpf__destroy(skel);
        return 1;
    }

    struct perf_buffer *pb = NULL;
    struct perf_buffer_opts opts = { .sample_cb = handle_event, .ctx = argv[1] };
    pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 8, &opts);
    if (!pb) {
        fprintf(stderr, "无法创建性能缓冲区\n");
        program_a_bpf__destroy(skel);
        return 1;
    }

    printf("程序正在运行。按 Ctrl+C 停止...\n");
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (!exiting) {
        err = perf_buffer__poll(pb, 100);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "轮询性能缓冲区出错: %d\n", err);
            break;
        }
        usleep(100000);
    }

    perf_buffer__free(pb);
    program_a_bpf__destroy(skel);
    printf("退出。\n");
    return 0;
}
