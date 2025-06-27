#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <signal.h> // 添加 signal.h 以定义 sig_atomic_t

#define MAX_PIDS 1024

extern volatile sig_atomic_t exiting;
extern int pipe_fds[MAX_PIDS][2];
extern int pipe_count;
extern pthread_mutex_t pipe_mutex;

#endif
