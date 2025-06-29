#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "collect.h"
#include "common.h"

#define INPUT_DIM 40
#define HIDDEN1_DIM 128
#define HIDDEN2_DIM 64
#define OUTPUT_DIM 2
#define ROWS_PER_INFERENCE 10
#define COLS_PER_ROW 4
#define MAX_ROWS 90

// 模型权重和偏置
float fc1_weight[INPUT_DIM * HIDDEN1_DIM];
float fc1_bias[HIDDEN1_DIM];
float fc2_weight[HIDDEN1_DIM * HIDDEN2_DIM];
float fc2_bias[HIDDEN2_DIM];
float fc3_weight[HIDDEN2_DIM * OUTPUT_DIM];
float fc3_bias[OUTPUT_DIM];

// 数据存储结构
struct pid_data {
    uint32_t pid;
    time_t timestamp;
    char **data;
    size_t data_count;
    size_t data_capacity;
    struct pid_data *next;
};

// 哈希表存储PID数据
#define HASH_SIZE 1024
struct pid_data *data_table[HASH_SIZE] = {0};

// ReLU激活函数
float relu(float x) {
    return x > 0 ? x : 0;
}

// 矩阵-向量乘法
void matmul(const float* matrix, const float* vector, float* result, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        result[i] = 0;
        for (int j = 0; j < cols; j++) {
            result[i] += matrix[i * cols + j] * vector[j];
        }
    }
}

// 加载模型权重
int load_weights(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        printf("无法打开权重文件: %s\n", filepath);
        return -1;
    }

    size_t read_count = 0;
    read_count += fread(fc1_weight, sizeof(float), INPUT_DIM * HIDDEN1_DIM, file);
    read_count += fread(fc1_bias, sizeof(float), HIDDEN1_DIM, file);
    read_count += fread(fc2_weight, sizeof(float), HIDDEN1_DIM * HIDDEN2_DIM, file);
    read_count += fread(fc2_bias, sizeof(float), HIDDEN2_DIM, file);
    read_count += fread(fc3_weight, sizeof(float), HIDDEN2_DIM * OUTPUT_DIM, file);
    read_count += fread(fc3_bias, sizeof(float), OUTPUT_DIM, file);

    fclose(file);
    if (read_count != INPUT_DIM * HIDDEN1_DIM + HIDDEN1_DIM + 
                     HIDDEN1_DIM * HIDDEN2_DIM + HIDDEN2_DIM + 
                     HIDDEN2_DIM * OUTPUT_DIM + OUTPUT_DIM) {
        printf("权重文件读取失败\n");
        return -1;
    }
    printf("模型权重加载成功\n");
    return 0;
}

// 前向传播
void forward(float* input, float* output) {
    float hidden1[HIDDEN1_DIM];
    float hidden2[HIDDEN2_DIM];

    matmul(fc1_weight, input, hidden1, HIDDEN1_DIM, INPUT_DIM);
    for (int i = 0; i < HIDDEN1_DIM; i++) {
        hidden1[i] = relu(hidden1[i] + fc1_bias[i]);
    }

    matmul(fc2_weight, hidden1, hidden2, HIDDEN2_DIM, HIDDEN1_DIM);
    for (int i = 0; i < HIDDEN2_DIM; i++) {
        hidden2[i] = relu(hidden2[i] + fc2_bias[i]);
    }

    matmul(fc3_weight, hidden2, output, OUTPUT_DIM, HIDDEN2_DIM);
    for (int i = 0; i < OUTPUT_DIM; i++) {
        output[i] += fc3_bias[i];
    }
}

// 计算哈希
static unsigned int hash_pid(uint32_t pid) {
    return pid % HASH_SIZE;
}

// 查找或创建PID数据节点
static struct pid_data *get_pid_data(uint32_t pid) {
    unsigned int index = hash_pid(pid);
    struct pid_data *entry = data_table[index];
    
    while (entry) {
        if (entry->pid == pid)
            return entry;
        entry = entry->next;
    }

    struct pid_data *new_entry = calloc(1, sizeof(struct pid_data));
    if (!new_entry) {
        perror("calloc pid_data");
        return NULL;
    }
    
    new_entry->pid = pid;
    new_entry->timestamp = time(NULL);
    new_entry->data_capacity = 10;
    new_entry->data = calloc(new_entry->data_capacity, sizeof(char *));
    if (!new_entry->data) {
        perror("calloc data array");
        free(new_entry);
        return NULL;
    }
    
    new_entry->next = data_table[index];
    data_table[index] = new_entry;
    return new_entry;
}

// 清理超过10秒的数据
static void cleanup_old_data() {
    time_t now = time(NULL);
    for (int i = 0; i < HASH_SIZE; i++) {
        struct pid_data *entry = data_table[i];
        struct pid_data *prev = NULL;
        
        while (entry) {
            if (now - entry->timestamp >= 10) {
                // 释放数据数组
                for (size_t j = 0; j < entry->data_count; j++) {
                    free(entry->data[j]);
                }
                free(entry->data);
                
                // 从链表中移除
                if (prev) {
                    prev->next = entry->next;
                } else {
                    data_table[i] = entry->next;
                }
                
                struct pid_data *temp = entry;
                entry = entry->next;
                free(temp);
            } else {
                prev = entry;
                entry = entry->next;
            }
        }
    }
}

// 添加数据到数组并进行推理
static int add_data_to_pid(uint32_t pid, const char *buffer, size_t len) {
    struct pid_data *entry = get_pid_data(pid);
    if (!entry)
        return -1;

    if (entry->data_count >= entry->data_capacity) {
        entry->data_capacity *= 2;
        char **new_data = realloc(entry->data, entry->data_capacity * sizeof(char *));
        if (!new_data) {
            perror("realloc data array");
            return -1;
        }
        entry->data = new_data;
    }

    entry->data[entry->data_count] = strndup(buffer, len);
    if (!entry->data[entry->data_count]) {
        perror("strndup");
        return -1;
    }
    
    entry->data_count++;
    entry->timestamp = time(NULL);

    // 每收到10的倍数行数据进行一次推理
    if (entry->data_count % ROWS_PER_INFERENCE == 0 && entry->data_count <= MAX_ROWS) {
        float accumulated_data[INPUT_DIM];
        int valid_rows = 0;

        // 使用所有累积的数据，最多90行，每次取最后10行
        size_t start_idx = entry->data_count > ROWS_PER_INFERENCE ? 
                           entry->data_count - ROWS_PER_INFERENCE : 0;
        for (size_t i = start_idx; i < entry->data_count && valid_rows < ROWS_PER_INFERENCE; i++) {
            char *line = entry->data[i];
            float values[COLS_PER_ROW];
            char *token = strtok(line, "\n");
            int col_idx = 0;

            // 解析每行中的4个事件值
            while (token && col_idx < COLS_PER_ROW) {
                if (sscanf(token, "  [%*d] %f\t", &values[col_idx]) == 1) {
                    col_idx++;
                }
                token = strtok(NULL, "\n");
            }

            if (col_idx == COLS_PER_ROW) {
                for (int j = 0; j < COLS_PER_ROW; j++) {
                    accumulated_data[valid_rows * COLS_PER_ROW + j] = values[j];
                }
                valid_rows++;
            }
        }

        if (valid_rows == ROWS_PER_INFERENCE) {
            float output[OUTPUT_DIM];
            forward(accumulated_data, output);
            int prediction = output[0] > output[1] ? 0 : 1;
            const char* label = prediction == 1 ? "恶意" : "良性";
            printf("PID %u 推理结果 (使用 %zu 行数据): %s (0=良性, 1=恶意, 预测值=%d)\n", 
                   pid, entry->data_count, label, prediction);
        }
    }

    return 0;
}

// 释放所有数据
static void cleanup_all_data() {
    for (int i = 0; i < HASH_SIZE; i++) {
        struct pid_data *entry = data_table[i];
        while (entry) {
            for (size_t j = 0; j < entry->data_count; j++) {
                free(entry->data[j]);
            }
            free(entry->data);
            struct pid_data *temp = entry;
            entry = entry->next;
            free(temp);
        }
        data_table[i] = NULL;
    }
}

// 接收线程
void *receive_thread(void *arg) {
    struct pollfd *pfds = NULL;
    int nfds = 0;

    // 加载模型权重
    if (load_weights("model_weights.bin") != 0) {
        printf("模型权重加载失败，退出接收线程\n");
        return NULL;
    }

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
            cleanup_old_data();
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
                    printf("Received raw data:\n%s\n", buffer);
                    
                    // 从buffer中提取PID
                    uint32_t pid;
                    if (sscanf(buffer, "[PID: %u]", &pid) == 1) {
                        if (add_data_to_pid(pid, buffer, len) == 0) {
                            printf("Stored data for PID %u, total entries: %zu\n", 
                                   pid, get_pid_data(pid)->data_count);
                        }
                    }
                }
            }
        }
        cleanup_old_data();
    }

    free(pfds);
    cleanup_all_data();
    return NULL;
}
