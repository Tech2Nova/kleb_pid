#include "perf_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

typedef struct {
    unsigned long user, system;
    unsigned long long total_system;
} CpuTime;

typedef struct {
    unsigned long resident;
    unsigned long virtual_size;
} MemoryUsage;

static CpuTime get_process_cpu_time() {
    FILE* stat = fopen("/proc/self/stat", "r");
    if (!stat) {
        fprintf(stderr, "Failed to open /proc/self/stat: %s\n", strerror(errno));
        return (CpuTime){0, 0, 0};
    }
    char line[256];
    if (!fgets(line, sizeof(line), stat)) {
        fclose(stat);
        fprintf(stderr, "Failed to read /proc/self/stat: %s\n", strerror(errno));
        return (CpuTime){0, 0, 0};
    }
    fclose(stat);

    char* token = strtok(line, " ");
    for (int i = 0; i < 13 && token; i++) token = strtok(NULL, " "); // 跳到 utime
    unsigned long utime = token ? strtoul(token, NULL, 10) : 0;
    token = strtok(NULL, " "); // stime
    unsigned long stime = token ? strtoul(token, NULL, 10) : 0;
    return (CpuTime){utime, stime, 0};
}

static CpuTime get_system_cpu_time() {
    FILE* stat = fopen("/proc/stat", "r");
    if (!stat) {
        fprintf(stderr, "Failed to open /proc/stat: %s\n", strerror(errno));
        return (CpuTime){0, 0, 0};
    }
    char line[256];
    if (!fgets(line, sizeof(line), stat)) {
        fclose(stat);
        fprintf(stderr, "Failed to read /proc/stat: %s\n", strerror(errno));
        return (CpuTime){0, 0, 0};
    }
    fclose(stat);

    char* fields[10];
    int field_count = 0;
    char* token = strtok(line, " ");
    while (token && field_count < 10) {
        fields[field_count++] = token;
        token = strtok(NULL, " ");
    }
    if (field_count < 5) {
        fprintf(stderr, "/proc/stat has insufficient fields\n");
        return (CpuTime){0, 0, 0};
    }

    unsigned long user = strtoul(fields[1], NULL, 10);
    unsigned long nice = strtoul(fields[2], NULL, 10);
    unsigned long system = strtoul(fields[3], NULL, 10);
    unsigned long idle = strtoul(fields[4], NULL, 10);
    return (CpuTime){0, 0, user + nice + system + idle};
}

static double calculate_cpu_usage(const CpuTime* prev_proc, const CpuTime* curr_proc,
                                 const CpuTime* prev_sys, const CpuTime* curr_sys) {
    unsigned long proc_diff = (curr_proc->user + curr_proc->system) -
                              (prev_proc->user + prev_proc->system);
    unsigned long long sys_diff = curr_sys->total_system - prev_sys->total_system;
    return (sys_diff == 0) ? 0.0 : (100.0 * proc_diff / sys_diff);
}

static MemoryUsage get_memory_usage() {
    FILE* status = fopen("/proc/self/status", "r");
    if (!status) {
        fprintf(stderr, "Failed to open /proc/self/status: %s\n", strerror(errno));
        return (MemoryUsage){0, 0};
    }
    char line[256];
    unsigned long vm_rss = 0, vm_size = 0;
    while (fgets(line, sizeof(line), status)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%lu", &vm_rss);
        } else if (strncmp(line, "VmSize:", 7) == 0) {
            sscanf(line + 7, "%lu", &vm_size);
        }
    }
    fclose(status);
    return (MemoryUsage){vm_rss, vm_size};
}

static void* monitor_thread_func(void* arg) {
    PerformanceMonitor* monitor = (PerformanceMonitor*)arg;
    CpuTime prev_proc_time = get_process_cpu_time();
    CpuTime prev_sys_time = get_system_cpu_time();

    while (monitor->running) {
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

        CpuTime curr_proc_time = get_process_cpu_time();
        CpuTime curr_sys_time = get_system_cpu_time();
        double cpu = calculate_cpu_usage(&prev_proc_time, &curr_proc_time,
                                        &prev_sys_time, &curr_sys_time);
        MemoryUsage mem = get_memory_usage();

        pthread_mutex_lock(&monitor->data_mutex);
        if (monitor->data_count < MAX_DATA_POINTS) {
            monitor->data[monitor->data_count] = (PerformanceData){
                .timestamp = {0, 0},
                .cpu_usage = cpu,
                .ram_usage = mem.resident,
                .virtual_mem = mem.virtual_size
            };
            clock_gettime(CLOCK_REALTIME, &monitor->data[monitor->data_count].timestamp);
            monitor->data_count++;
        }
        pthread_mutex_unlock(&monitor->data_mutex);

        prev_proc_time = curr_proc_time;
        prev_sys_time = curr_sys_time;

        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms < monitor->interval_ms) {
            usleep((monitor->interval_ms - elapsed_ms) * 1000);
        }
    }
    return NULL;
}

PerformanceMonitor* perf_monitor_create(int interval_ms) {
    PerformanceMonitor* monitor = (PerformanceMonitor*)calloc(1, sizeof(PerformanceMonitor));
    if (!monitor) {
        fprintf(stderr, "Failed to allocate PerformanceMonitor: %s\n", strerror(errno));
        return NULL;
    }
    monitor->interval_ms = interval_ms;
    monitor->running = 0;
    monitor->data_count = 0;
    if (pthread_mutex_init(&monitor->data_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex: %s\n", strerror(errno));
        free(monitor);
        return NULL;
    }
    return monitor;
}

void perf_monitor_destroy(PerformanceMonitor* monitor) {
    if (!monitor) return;
    perf_monitor_stop(monitor);
    pthread_mutex_destroy(&monitor->data_mutex);
    free(monitor);
}

void perf_monitor_start(PerformanceMonitor* monitor) {
    if (!monitor || monitor->running) return;
    monitor->running = 1;
    if (pthread_create(&monitor->monitor_thread, NULL, monitor_thread_func, monitor) != 0) {
        fprintf(stderr, "Failed to create monitor thread: %s\n", strerror(errno));
        monitor->running = 0;
        return;
    }
}

void perf_monitor_stop(PerformanceMonitor* monitor) {
    if (!monitor || !monitor->running) return;
    monitor->running = 0;
    pthread_join(monitor->monitor_thread, NULL);
}

void perf_monitor_save_csv(PerformanceMonitor* monitor, const char* filename) {
    if (!monitor) return;
    pthread_mutex_lock(&monitor->data_mutex);
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open %s: %s\n", filename, strerror(errno));
        pthread_mutex_unlock(&monitor->data_mutex);
        return;
    }

    fprintf(file, "Timestamp,CPU(%%),RAM(KB),VirtualMem(KB)\n");
    for (int i = 0; i < monitor->data_count; i++) {
        char time_str[32];
        struct tm local_time;
        localtime_r(&monitor->data[i].timestamp.tv_sec, &local_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &local_time);
        fprintf(file, "%s.%03ld,%.2f,%lu,%lu\n",
                time_str,
                monitor->data[i].timestamp.tv_nsec / 1000000,
                monitor->data[i].cpu_usage,
                monitor->data[i].ram_usage,
                monitor->data[i].virtual_mem);
    }
    fclose(file);
    pthread_mutex_unlock(&monitor->data_mutex);
}
