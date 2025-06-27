#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <inttypes.h>
#include "collect.h"
#include "common.h"

// 哈希表节点，用于存储 PID 和 HPC 数据
struct hpc_data {
    uint32_t pid;
    uint64_t values[TOTAL_EVENTS][TOTAL_SAMPLES];
    char event_names[TOTAL_EVENTS][32];
    int sample_count;
    struct hpc_data *next;
};

// 哈希表大小
#define HPC_HASH_SIZE 1024
static struct hpc_data *hpc_table[HPC_HASH_SIZE] = {0};
static pthread_mutex_t hpc_mutex = PTHREAD_MUTEX_INITIALIZER;

// 哈希函数
static unsigned int hash_pid(uint32_t pid) {
    return pid % HPC_HASH_SIZE;
}

// 添加或更新 HPC 数据
static void store_hpc_data(uint32_t pid, const char *event_name, int sample_idx, uint64_t value) {
    unsigned int index = hash_pid(pid);
    pthread_mutex_lock(&hpc_mutex);

    struct hpc_data *entry = hpc_table[index];
    while (entry && entry->pid != pid) {
        entry = entry->next;
    }

    if (!entry) {
        entry = malloc(sizeof(struct hpc_data));
        if (!entry) {
            perror("malloc hpc_data");
            pthread_mutex_unlock(&hpc_mutex);
            return;
        }
        memset(entry, 0, sizeof(struct hpc_data));
        entry->pid = pid;
        entry->next = hpc_table[index];
        hpc_table[index] = entry;
    }

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        if (entry->event_names[i][0] == '\0') {
            strncpy(entry->event_names[i], event_name, 31);
            entry->event_names[i][31] = '\0';
            break;
        }
        if (strcmp(entry->event_names[i], event_name) == 0) { // 修正语法错误，移除 Ang
            entry->values[i][sample_idx] = value;
            if (sample_idx + 1 > entry->sample_count) {
                entry->sample_count = sample_idx + 1;
            }
            break;
        }
    }

    pthread_mutex_unlock(&hpc_mutex);
}

// 清理哈希表
static void cleanup_hpc_table() {
    for (int i = 0; i < HPC_HASH_SIZE; i++) {
        struct hpc_data *entry = hpc_table[i];
        while (entry) {
            struct hpc_data *temp = entry;
            entry = entry->next;
            free(temp);
        }
        hpc_table[i] = NULL;
    }
}

// 解析接收到的数据
static void parse_data(const char *buffer) {
    uint32_t pid;
    int start_sample, end_sample;
    char event_name[32];
    int sample_idx;
    uint64_t value;

    // 调试：打印接收到的缓冲区内容
    printf("Received data: %s\n", buffer);

    // 解析 [PID: %d] Samples %d–%d:
    if (sscanf(buffer, "[PID: %d] Samples %d–%d:", &pid, &start_sample, &end_sample) != 3) {
        fprintf(stderr, "Failed to parse header: %s\n", buffer);
        return;
    }

    const char *ptr = buffer;
    while ((ptr = strstr(ptr, "Event: "))) {
        ptr += 7;
        if (sscanf(ptr, "%31s", event_name) != 1) {
            fprintf(stderr, "Failed to parse event name\n");
            continue;
        }
        ptr = strchr(ptr, '\n') + 1;

        for (int j = start_sample; j <= end_sample; j++) {
            if (sscanf(ptr, "  [%02d] %" PRIu64 "\t", &sample_idx, &value) == 2) {
                store_hpc_data(pid, event_name, sample_idx, value);
                ptr = strchr(ptr, '\t') + 1;
            } else {
                fprintf(stderr, "Failed to parse sample data: %s\n", ptr);
            }
        }
    }
}

// 接收线程
void *receive_thread(void *arg) {
    struct pollfd *pfds = NULL;
    int nfds = 0;

    while (!exiting) {
        pthread_mutex_lock(&pipe_mutex);
        if (nfds < pipe_count) {
            pfds = realloc(pfds, pipe_count * sizeof(struct pollfd));
            if (!pfds) {
                perror("realloc pollfd");
                pthread_mutex_unlock(&pipe_mutex);
                break;
            }
            for (int i = nfds; i < pipe_count; i++) {
                pfds[i].fd = pipe_fds[i][0];
                pfds[i].events = POLLIN;
            }
            nfds = pipe_count;
        }
        pthread_mutex_unlock(&pipe_mutex);

        if (nfds == 0) {
            usleep(100000);
            continue;
        }

        int ret = poll(pfds, nfds, 100);
        if (ret < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (pfds[i].revents & POLLIN) {
                char buffer[1024];
                ssize_t len = read(pfds[i].fd, buffer, sizeof(buffer) - 1);
                if (len > 0) {
                    buffer[len] = '\0';
                    parse_data(buffer);
                }
            }
        }
    }

    free(pfds);
    cleanup_hpc_table();
    return NULL;
}
