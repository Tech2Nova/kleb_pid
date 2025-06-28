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

void collect_perf_events(int target_pid, const char *events[4], int pipe_fd) {
    int fds[TOTAL_EVENTS];
    uint64_t values[TOTAL_EVENTS][TOTAL_SAMPLES] = {0};
    const char *used_names[TOTAL_EVENTS];
    int types[TOTAL_EVENTS], configs[TOTAL_EVENTS];
    char buffer[1024]; // 用于格式化数据

    for (int i = 0; i < TOTAL_EVENTS; i++) {
        if (!events || !events[i]) {
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
                continue;
            }
            if (sample > 0) {
                values[i][sample] = current_values[i] - values[i][sample - 1];
            } else {
                values[i][sample] = current_values[i];
            }
        }

        // 将数据格式化为字符串并通过管道传递
        if ((sample + 1) % PRINT_EVERY == 0) {
            int start = sample + 1 - PRINT_EVERY;
            int len = snprintf(buffer, sizeof(buffer), "[PID: %d] Samples %d–%d:\n", target_pid, start, sample);
            for (int i = 0; i < TOTAL_EVENTS; i++) {
                len += snprintf(buffer + len, sizeof(buffer) - len, "Event: %-20s\n", used_names[i]);
                for (int j = start; j <= sample; j++) {
                    len += snprintf(buffer + len, sizeof(buffer) - len, "  [%02d] %" PRIu64 "\t", j, values[i][j]);
                }
                len += snprintf(buffer + len, sizeof(buffer) - len, "\n");
            }
            
            ssize_t written = write(pipe_fd, buffer, len);
	    if (written == -1) {
	        fprintf(stderr, "Failed to write to pipe: %s\n", strerror(errno));
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
