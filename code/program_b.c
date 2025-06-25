/* program_b.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/types.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    int pid = atoi(argv[1]);
    printf("[program_b] Received PID: %d\n", pid);

    // 将当前进程 PID 添加到 exclude_pids 映射
    int map_fd = bpf_obj_get("/sys/fs/bpf/exclude_pids");
    if (map_fd < 0) {
        fprintf(stderr, "Failed to get exclude_pids map: %d\n", map_fd);
    } else {
        __u32 curr_pid = getpid();
        __u8 val = 1;
        int err = bpf_map_update_elem(map_fd, &curr_pid, &val, BPF_ANY);
        if (err) {
            fprintf(stderr, "Failed to update exclude_pids map for PID %d\n", curr_pid);
        }
        close(map_fd);
    }

    // 构造 K-Leb 命令
    char cmd[512];
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "../tmp/hpc_pid_%d.csv", pid);
    snprintf(cmd, sizeof(cmd), 
             "sudo ../K-LEB-Intel-demo/ioctl_start -e BR_RET,BR_MISP_RET,LOAD,STORE -t 1 -o %s %d",
             log_path, pid);

    // 执行 K-Leb 命令
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[program_b] Failed to execute K-Leb for PID %d\n", pid);
        return 1;
    }

    printf("[program_b] HPC data collected for PID %d in %s\n", pid, log_path);
    return 0;
}
