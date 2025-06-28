#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define INPUT_DIM 120
#define HIDDEN1_DIM 128
#define HIDDEN2_DIM 64
#define OUTPUT_DIM 2
#define ROWS_PER_INFERENCE 30
#define COLS_PER_ROW 4
#define MAX_ROWS 90

// 模型权重和偏置
float fc1_weight[INPUT_DIM * HIDDEN1_DIM];
float fc1_bias[HIDDEN1_DIM];
float fc2_weight[HIDDEN1_DIM * HIDDEN2_DIM];
float fc2_bias[HIDDEN2_DIM];
float fc3_weight[HIDDEN2_DIM * OUTPUT_DIM];
float fc3_bias[OUTPUT_DIM];

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

// 获取当前时间（秒）
double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 推理函数
void inference(const char* sample_path, const char* model_path) {
    // 加载模型权重
    if (load_weights(model_path) != 0) {
        return;
    }

    // 读取样本数据
    FILE* file = fopen(sample_path, "r");
    if (!file) {
        printf("无法打开样本文件: %s\n", sample_path);
        return;
    }

    // 跳过标题行
    char line[1024];
    if (!fgets(line, sizeof(line), file)) {
        printf("样本文件为空\n");
        fclose(file);
        return;
    }

    // 一次性读取所有数据
    float data[MAX_ROWS * COLS_PER_ROW];
    int row_count = 0;
    while (fgets(line, sizeof(line), file) && row_count < MAX_ROWS) {
        float values[COLS_PER_ROW];
        if (sscanf(line, "%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3]) != 4) {
            printf("第 %d 行格式错误: %s", row_count + 2, line);
            continue;
        }
        for (int i = 0; i < COLS_PER_ROW; i++) {
            data[row_count * COLS_PER_ROW + i] = values[i];
        }
        row_count++;
    }
    fclose(file);

    if (row_count != MAX_ROWS) {
        printf("警告：样本文件行数为 %d，期望为 %d 行\n", row_count, MAX_ROWS);
        return;
    }

    // 按批次处理数据
    float accumulated_data[INPUT_DIM];
    double start_time = get_time();
    double target_interval = 0.1; // 0.1秒间隔

    for (int batch = 0; batch < row_count / ROWS_PER_INFERENCE; batch++) {
        // 提取30行数据（120维）
        for (int i = 0; i < ROWS_PER_INFERENCE * COLS_PER_ROW; i++) {
            accumulated_data[i] = data[batch * ROWS_PER_INFERENCE * COLS_PER_ROW + i];
        }

        // 推理
        double batch_start_time = get_time();
        float output[OUTPUT_DIM];
        forward(accumulated_data, output);
        int prediction = output[0] > output[1] ? 0 : 1;
        const char* label = prediction == 1 ? "恶意" : "良性";
        double inference_time = get_time() - batch_start_time;

        // 输出结果
        printf("时间: %.1f秒, 预测结果: %s (0=良性, 1=恶意, 预测值=%d)\n",
               get_time() - start_time, label, prediction);

        // 控制时间间隔
        double elapsed = get_time() - start_time;
        double expected_time = (batch + 1) * target_interval;
        if (elapsed < expected_time) {
            struct timespec sleep_time;
            sleep_time.tv_sec = 0;
            sleep_time.tv_nsec = (long)((expected_time - elapsed) * 1e9);
            nanosleep(&sleep_time, NULL);
        }
    }
}

int main() {
    const char* sample_path = "8001.csv";
    const char* model_path = "model_weights.bin";
    inference(sample_path, model_path);
    return 0;
}



