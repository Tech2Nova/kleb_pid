#ifndef COLLECT_PERF_EVENTS_H
#define COLLECT_PERF_EVENTS_H
#include <stdint.h>

void collect_perf_events(int target_pid, const char *events[4]);

#endif
