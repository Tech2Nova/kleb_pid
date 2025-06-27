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
                return 1; // PID 在 5 秒内已处理
            } else {
                // 更新时间戳
                entry->timestamp = now;
                return 0;
            }
        }
        entry = entry->next;
    }

    // 新 PID，添加到哈希表
    struct pid_entry *new_entry = malloc(sizeof(struct pid_entry));
    if (!new_entry) {
        perror("malloc pid_entry");
        return 0; // 内存分配失败，允许处理
    }
    new_entry->pid = pid;
    new_entry->timestamp = now;
    new_entry->next = pid_table[index];
    pid_table[index] = new_entry;
    return 0;
}

// 清理哈希表（程序退出时）
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

// Thread worker data
struct thread_arg {
    int pid;
    const char **events;
};

void *monitor_thread(void *arg) {
    struct thread_arg *targ = arg;
    collect_perf_events(targ->pid, targ->events);
    free(targ);
    return NULL;
}

// BPF and perf event handling
static volatile sig_atomic_t exiting = 0;

void handle_signal(int sig) {
    exiting = 1;
}

static void handle_event(void *ctx, int cpu, void *data, __u32 data_sz) {
    if (data_sz < sizeof(uint32_t)) return;
    uint32_t pid = *(uint32_t *)data;

    // 检查 PID 是否在 5 秒内已处理
    if (is_pid_recent(pid)) {
        return; // 忽略重复 PID
    }

    printf("[execve] Caught process PID: %d\n", pid);

    pthread_t tid;
    struct thread_arg *targ = malloc(sizeof(*targ));
    if (!targ) {
        perror("malloc");
        return;
    }
    targ->pid = pid;
    targ->events = NULL;
    if (pthread_create(&tid, NULL, monitor_thread, targ) != 0) {
        perror("pthread_create");
        free(targ);
    } else {
        pthread_detach(tid);
    }
    return;
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
    printf("Exiting.\n");
    return 0;
}
