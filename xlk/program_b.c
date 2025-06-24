// program_b.c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    int pid = atoi(argv[1]);
    printf("[program_b] Received PID: %d\n", pid);

    // 使用 zenity 弹窗（需要安装 zenity）
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "zenity --info --text='Execve triggered by PID: %d' --title='BPF Alert'", pid);
    system(cmd);

    return 0;
}
