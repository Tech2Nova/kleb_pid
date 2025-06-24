#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "program_a_bpf.skel.h"

static volatile sig_atomic_t exiting = 0;
struct perf_buffer *pb = NULL;

void handle_signal(int sig) {
    exiting = 1;
}

static int handle_event(void *ctx, int cpu, void *data, __u32 data_sz) {
    if (data_sz < sizeof(uint32_t)) return 0;
    uint32_t pid = *(uint32_t *)data;

    printf("[execve] Caught process PID: %d\n", pid);

    pid_t child_pid = fork();
    if (child_pid == -1) {
        perror("fork failed");
        return 0;
    } else if (child_pid == 0) {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%u", pid);
        execl("./program_b", "program_b", pid_str, (char *)NULL);
        perror("execl failed");
        _exit(1);
    }
    return 0;
}

int main() {
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

    // 使用 perf_buffer_opts 结构体配置回调函数
    struct perf_buffer_opts opts = {
        .sample_cb = handle_event,
        .ctx = NULL,
    };
    pb = perf_buffer__new(bpf_map__fd(skel->maps.events), 8, &opts);
    if (!pb) {
        fprintf(stderr, "Failed to create perf buffer\n");
        program_a_bpf__destroy(skel);
        return 1;
    }

    printf("Program A is running. Press Ctrl+C to stop...\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (!exiting) {
        err = perf_buffer__poll(pb, 100);
        if (err < 0 && err != -EINTR) {
            fprintf(stderr, "Error polling perf buffer: %d\n", err);
            break;
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    perf_buffer__free(pb);
    program_a_bpf__destroy(skel);
    printf("Exiting.\n");
    return 0;
}
