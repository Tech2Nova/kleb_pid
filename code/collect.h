#ifndef COLLECT_H
#define COLLECT_H

#define TOTAL_EVENTS 4
#define TOTAL_SAMPLES 30

void collect_perf_events(int target_pid, const char *events[TOTAL_EVENTS], int pipe_fd);

#endif
