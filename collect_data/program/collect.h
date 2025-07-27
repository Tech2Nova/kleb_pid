#ifndef COLLECT_H
#define COLLECT_H

void collect_perf_events(int target_pid, const char *events[4], const char *sample_dir);

#endif // COLLECT_H
