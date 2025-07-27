#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <time.h>
#include <pthread.h>

#define MAX_DATA_POINTS 3600 // 最大存储1小时的数据（每秒1次）

typedef struct {
    struct timespec timestamp;
    double cpu_usage;
    unsigned long ram_usage;    // KB
    unsigned long virtual_mem;  // KB
} PerformanceData;

typedef struct {
    int interval_ms;
    int running;
    PerformanceData data[MAX_DATA_POINTS];
    int data_count;
    pthread_mutex_t data_mutex;
    pthread_t monitor_thread;
} PerformanceMonitor;

PerformanceMonitor* perf_monitor_create(int interval_ms);
void perf_monitor_destroy(PerformanceMonitor* monitor);
void perf_monitor_start(PerformanceMonitor* monitor);
void perf_monitor_stop(PerformanceMonitor* monitor);
void perf_monitor_save_csv(PerformanceMonitor* monitor, const char* filename);

#endif // PERF_MONITOR_H
