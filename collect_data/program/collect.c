#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "perf_monitor.h"

#define TOTAL_EVENTS 4
#define SAMPLE_INTERVAL_MS 10
#define TOTAL_SAMPLES 1000
#define PRINT_EVERY 500

typedef struct {
    const char *name;
    __u32 type;
    __u64 config;
} EventDef;

static const EventDef default_events[TOTAL_EVENTS] = {
    { "branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
    { "cache-references", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES },
    { "cache-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES },
    { "bus-cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES },
};

int parse_event(const char *name, __u32 *type, __u64 *config) {
    if (strcmp(name, "branches") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    } else if (strcmp(name, "cache-references") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_CACHE_REFERENCES;
    } else if (strcmp(name, "cache-misses") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_CACHE_MISSES;
    } else if (strcmp(name, "bus-cycles") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_BUS_CYCLES;
    } else {
        fprintf(stderr, "不支持的事件名称: %s\n", name);
        return -1;
    }
    return 0;
}

struct perf_event_attr create_event_attr(__u32 type, __u64 config) {
    struct perf_event_attr attr = {0};
    attr.size = sizeof(attr);
    attr.type = type;
    attr.config = config;
    attr.disabled = 1;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 1;
    return attr;
}

void collect_perf_events(int target_pid, const char *events[4], const char *sample_dir) {
    PerformanceMonitor* monitor = perf_monitor_create(1000);
    if (!monitor) {
        fprintf(stderr, "创建性能监控器失败\n");
        return;
    }
    perf_monitor_start(monitor);

    int fds[TOTAL_EVENTS];
    uint64_t values[TOTAL_EVENTS][TOTAL_SAMPLES] = {0};
    const char *used_names[TOTAL_EVENTS];
    __u32 types[TOTAL_EVENTS];
    __u64 configs[TOTAL_EVENTS];

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        if (!events || !events[i]) {
            types[i] = default_events[i].type;
            configs[i] = default_events[i].config;
            used_names[i] = default_events[i].name;
        } else {
            if (parse_event(events[i], &types[i], &configs[i]) != 0) {
                perf_monitor_destroy(monitor);
                return;
            }
            used_names[i] = events[i];
        }

        struct perf_event_attr attr = create_event_attr(types[i], configs[i]);
        fds[i] = syscall(__NR_perf_event_open, &attr, target_pid, -1, -1, 0);
        if (fds[i] == -1) {
            fprintf(stderr, "perf_event_open 失败 for %s: %s\n", used_names[i], strerror(errno));
            perf_monitor_destroy(monitor);
            return;
        }

        ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
        ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
    }

    // 创建样本子目录
    if (mkdir(sample_dir, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "创建样本目录 %s 失败: %s\n", sample_dir, strerror(errno));
        for (int i = 0; i < TOTAL_EVENTS; i++) {
            ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
            close(fds[i]);
        }
        perf_monitor_destroy(monitor);
        return;
    }

    // 性能计数器数据文件名
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/perf_output_%d.csv", sample_dir, target_pid);
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "打开文件 %s 失败: %s\n", filename, strerror(errno));
        for (int i = 0; i < TOTAL_EVENTS; i++) {
            ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
            close(fds[i]);
        }
        perf_monitor_destroy(monitor);
        return;
    }

    fprintf(fp, "sample");
    for (int i = 0; i < TOTAL_EVENTS; i++) {
        fprintf(fp, ",%s", used_names[i]);
    }
    fprintf(fp, "\n");

    uint64_t prev_values[TOTAL_EVENTS] = {0};

    for (int sample = 0; sample < TOTAL_SAMPLES; sample++) {
        usleep(SAMPLE_INTERVAL_MS * 1000);
        uint64_t current_values[TOTAL_EVENTS];

        for (int i = 0; i < TOTAL_EVENTS; i++) {
            ssize_t ret = read(fds[i], &current_values[i], sizeof(current_values[i]));
            if (ret != sizeof(current_values[i])) {
                fprintf(stderr, "读取性能事件 %s 失败: %s\n", used_names[i], strerror(errno));
                fclose(fp);
                for (int j = 0; j < TOTAL_EVENTS; j++) {
                    ioctl(fds[j], PERF_EVENT_IOC_DISABLE, 0);
                    close(fds[j]);
                }
                perf_monitor_destroy(monitor);
                return;
            }
        }

        fprintf(fp, "%d", sample);
        for (int i = 0; i < TOTAL_EVENTS; i++) {
            uint64_t delta = sample == 0 ? 0 : current_values[i] - prev_values[i];
            values[i][sample] = delta;
            prev_values[i] = current_values[i];
            fprintf(fp, ",%" PRIu64, delta);
        }
        fprintf(fp, "\n");

        if ((sample + 1) % PRINT_EVERY == 0) {
            int start = sample + 1 - PRINT_EVERY;
            printf("\n[PID: %d] 样本 %d–%d:\n", target_pid, start, sample);
            for (int i = 0; i < TOTAL_EVENTS; i++) {
                printf("事件: %-20s\n", used_names[i]);
                for (int j = start; j <= sample; j++) {
                    printf("  [%02d] %" PRIu64 "\t", j, values[i][j]);
                }
                printf("\n");
            }
        }
    }

    fclose(fp);

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
        close(fds[i]);
    }

    // 性能监控数据文件名
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &local_time);
    char usage_filename[128];
    snprintf(usage_filename, sizeof(usage_filename), "%s/usage_%s.csv", sample_dir, time_str);
    perf_monitor_save_csv(monitor, usage_filename);
    perf_monitor_destroy(monitor);
}
