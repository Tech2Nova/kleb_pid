#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <time.h>
#include "program_a_bpf.skel.h"
#include "collect.h"
#include "common.h"

// 哈希表节点，用于存储 PID 和时间戳
struct pid_entry {
    uint32_t pid;
    time_t timestamp;
    struct pid_entry *next;
};

// 哈希表大小
#define HASH_SIZE 1024

// 全局哈希表
struct pid_entry *pid_table[HASH_SIZE] = {0};

// 定义全局变量（在 common.h 中声明）
volatile sig_atomic_t exiting = 0;
int pipe_fds[MAX_PIDS][2];
int pipe_count = 0;
pthread_mutex_t pipe_mutex = PTHREAD_MUTEX_INITIALIZER;

// 哈希函数
static unsigned int hash_pid(uint32_t pid) {
    return pid % HASH_SIZE;
}

// 检查 PID 是否在 5 秒内已处理
static int is_pid_recent(uint32_t pid) {
    unsigned int index = hash_pid(pid);
    struct pid_entry *entry = pid_table[index];
    time_t now = time(NULL);

    while (entry) {
        if (entry->pid == pid) {
            if (now - entry->timestamp < 5) {
                return 1;
            } else {
                entry->timestamp = now;
                return 0;
            }
        }
        entry = entry->next;
    }

    struct pid_entry *new_entry = malloc(sizeof(struct pid_entry));
    if (!new_entry) {
        perror("malloc pid_entry");
        return 0;
    }
    new_entry->pid = pid;
    new_entry->timestamp = now;
    new_entry->next = pid_table[index];
    pid_table[index] = new_entry;
    return 0;
}

// 清理哈希表
static void cleanup_pid_table() {
    for (int i = 0; i < HASH_SIZE; i++) {
        struct pid_entry *entry = pid_table[i];
        while (entry) {
            struct pid_entry *temp = entry;
            entry = entry->next;
            free(temp);
        }
        pid_table[i] = NULL;
    }
}

// 清理管道
static void cleanup_pipes() {
    pthread_mutex_lock(&pipe_mutex);
    for (int i = 0; i < pipe_count; i++) {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }
    pipe_count = 0;
    pthread_mutex_unlock(&pipe_mutex);
}

// Thread worker data
struct thread_arg {
    int pid;
    const char **events;
    int pipe_fd;
};

// 接收线程函数声明
void *receive_thread(void *arg);

// 监控线程函数
void *monitor_thread(void *arg) {
    struct thread_arg *targ = arg;
    collect_perf_events(targ->pid, targ->events, targ->pipe_fd);
    free(targ);
    return NULL;
}

// BPF and perf event handling
void handle_signal(int sig) {
    exiting = 1;
}

static void handle_event(void *ctx, int cpu, void *data, __u32 data_sz) {
    if (data_sz < sizeof(uint32_t)) return;
    uint32_t pid = *(uint32_t *)data;

    if (is_pid_recent(pid)) {
        return;
    }

    printf("[execve] Caught process PID: %d\n", pid);

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return;
    }

    pthread_mutex_lock(&pipe_mutex);
    if (pipe_count >= MAX_PIDS) {
        fprintf(stderr, "Too many PIDs\n");
        close(fd[0]);
        close(fd[1]);
        pthread_mutex_unlock(&pipe_mutex);
        return;
    }
    pipe_fds[pipe_count][0] = fd[0];
    pipe_fds[pipe_count][1] = fd[1];
    pipe_count++;
    pthread_mutex_unlock(&pipe_mutex);

    pthread_t tid;
    struct thread_arg *targ = malloc(sizeof(*targ));
    if (!targ) {
        perror("malloc");
        close(fd[0]);
        close(fd[1]);
        return;
    }
    targ->pid = pid;
    targ->events = NULL;
    targ->pipe_fd = fd[1];
    if (pthread_create(&tid, NULL, monitor_thread, targ) != 0) {
        perror("pthread_create");
        close(fd[0]);
        close(fd[1]);
        free(targ);
    } else {
        pthread_detach(tid);
    }
}

int main(int argc, char **argv) {
    struct program_a_bpf *skel;
    int err;

    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &rlim);

    skel = program_a_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }
    err = program_a_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program\n");
        program_a_bpf__destroy(skel);
        return 1;
    }

    struct perf_buffer *pb = NULL;
    struct perf_buffer_opts opts = { .sample_cb = handle_event, .ctx = NULL };
    pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 8, &opts);
    if (!pb) {
        fprintf(stderr, "Failed to create perf buffer\n");
        program_a_bpf__destroy(skel);
        return 1;
    }

    // 启动接收线程
    pthread_t recv_tid;
    if (pthread_create(&recv_tid, NULL, receive_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create receive thread\n");
        perf_buffer__free(pb);
        program_a_bpf__destroy(skel);
        return 1;
    }
    pthread_detach(recv_tid);

    printf("Program is running. Press Ctrl+C to stop...\n");
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (!exiting) {
        err = perf_buffer__poll(pb, 100);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Error polling perf buffer: %d\n", err);
            break;
        }
        usleep(100000);
    }

    perf_buffer__free(pb);
    program_a_bpf__destroy(skel);
    cleanup_pid_table();
    cleanup_pipes();
    printf("Exiting.\n");
    return 0;
}
