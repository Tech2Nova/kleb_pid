#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <inttypes.h>
#include "collect.h"
#include "common.h"

// 接收线程
void *receive_thread(void *arg) {
    struct pollfd *pfds = NULL;
    int nfds = 0;

    while (!exiting) {
        pthread_mutex_lock(&pipe_mutex);
        if (nfds < pipe_count) {
            pfds = realloc(pfds, pipe_count * sizeof(struct pollfd));
            if (!pfds) {
                perror("realloc pollfd");
                pthread_mutex_unlock(&pipe_mutex);
                break;
            }
            for (int i = nfds; i < pipe_count; i++) {
                pfds[i].fd = pipe_fds[i][0];
                pfds[i].events = POLLIN;
            }
            nfds = pipe_count;
        }
        pthread_mutex_unlock(&pipe_mutex);

        if (nfds == 0) {
            usleep(100000);
            continue;
        }

        int ret = poll(pfds, nfds, 100);
        if (ret < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (pfds[i].revents & POLLIN) {
                char buffer[1024];
                ssize_t len = read(pfds[i].fd, buffer, sizeof(buffer) - 1);
                if (len > 0) {
                    buffer[len] = '\0';
                    printf("Received raw data:\n%s\n", buffer); // 只打印原始数据
                }
            }
        }
    }

    free(pfds);
    return NULL;
}
