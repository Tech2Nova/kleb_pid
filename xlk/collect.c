#define _GNU_SOURCE
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

#define TOTAL_EVENTS 4
#define SAMPLE_INTERVAL_MS 10
#define TOTAL_SAMPLES 30
#define PRINT_EVERY 10

typedef struct {
    const char *name;
    int type;
    int config;
} EventDef;

// 默认事件（适合恶意软件检测）
static const EventDef default_events[TOTAL_EVENTS] = {
    { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS },
    { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
    { "branch-instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
    { "branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES },
};

// 简单事件名解析器
int parse_event(const char *name, int *type, int *config) {
    if (strcmp(name, "instructions") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_INSTRUCTIONS;
    } else if (strcmp(name, "cycles") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_CPU_CYCLES;
    } else if (strcmp(name, "branch-instructions") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    } else if (strcmp(name, "branch-misses") == 0) {
        *type = PERF_TYPE_HARDWARE;
        *config = PERF_COUNT_HW_BRANCH_MISSES;
    } else {
        fprintf(stderr, "Unsupported event name: %s\n", name);
        return -1;
    }
    return 0;
}

struct perf_event_attr create_event_attr(int type, int config) {
    struct perf_event_attr attr = {
        .type = type,
        .config = config,
        .size = sizeof(attr),
        .disabled = 1,
        .exclude_kernel = 0,
        .exclude_hv = 1
    };
    return attr;
}

void collect_perf_events(int target_pid, const char *events[4]) {
    int fds[TOTAL_EVENTS];
    uint64_t values[TOTAL_EVENTS][TOTAL_SAMPLES] = {0};
    const char *used_names[TOTAL_EVENTS];
    int types[TOTAL_EVENTS], configs[TOTAL_EVENTS];

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        if (!events || !events[i]) {
            // 使用默认事件
            types[i] = default_events[i].type;
            configs[i] = default_events[i].config;
            used_names[i] = default_events[i].name;
        } else {
            if (parse_event(events[i], &types[i], &configs[i]) != 0)
                return;
            used_names[i] = events[i];
        }

        struct perf_event_attr attr = create_event_attr(types[i], configs[i]);
        fds[i] = syscall(__NR_perf_event_open, &attr, target_pid, -1, -1, 0);
        if (fds[i] == -1) {
            fprintf(stderr, "perf_event_open failed for %s: %s\n", used_names[i], strerror(errno));
            return;
        }

        ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
        ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
    }

    for (int sample = 0; sample < TOTAL_SAMPLES; sample++) {
        usleep(SAMPLE_INTERVAL_MS * 1000);
        uint64_t current_values[TOTAL_EVENTS];
        for (int i = 0; i < TOTAL_EVENTS; i++) {
            ssize_t ret = read(fds[i], &current_values[i], sizeof(current_values[i]));
            if (ret != sizeof(current_values[i])) {
                fprintf(stderr, "Failed to read perf event %s for PID %d: %s\n",
                        used_names[i], target_pid, strerror(errno));
                continue; // 继续处理其他事件
            }
            if (sample > 0) { // 避免第一次采样时访问 values[i][-1]
                values[i][sample] = current_values[i] - values[i][sample - 1];
            } else {
                values[i][sample] = current_values[i];
            }
        }

        if ((sample + 1) % PRINT_EVERY == 0) {
            int start = sample + 1 - PRINT_EVERY;
            printf("\n[PID: %d] Samples %d–%d:\n", target_pid, start, sample);
            for (int i = 0; i < TOTAL_EVENTS; i++) {
                printf("Event: %-20s\n", used_names[i]);
                for (int j = start; j <= sample; j++) {
                    printf("  [%02d] %" PRIu64 "\t", j, values[i][j]);
                }
                printf("\n");
            }
        }
    }

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
        close(fds[i]);
    }
}

/*int main(int argc, char *argv[]){
    int target_pid = atoi(argv[1]);
    const char *events[] = {"instructions", "cycles", "branch-instructions", "branch-misses"};
    collect_perf_events(target_pid, events);
    return 0;
}*/
