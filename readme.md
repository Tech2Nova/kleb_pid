**代码结构**

```bash
(base) mz2@mz2-Precision-3530:~/Desktop/kleb_pid$ ls
code  execve_log.txt  K-LEB-Intel-demo  tmp
(base) mz2@mz2-Precision-3530:~/Desktop/dcx$ cd code
(base) mz2@mz2-Precision-3530:~/Desktop/kleb_pid/code$ ls
execve_log.txt   program_a_bpf.o       program_a_user.c   vmlinux.h
makefile         program_a_bpf.skel.h  program_b
Output.csv       program_a.h           program_b.c
program_a_bpf.c  program_a_user        program_b_out.txt
```

**program_a_bpf**：内核层组件，负责监控 execve 系统调用事件。捕获每个执行进程的进程 ID（PID），并将其传递给 program_a_user.c 进行后续处理。

**program_a_user**：用户空间程序，接收来自 program_a_bpf 的 PID。对于每个捕获的 PID，调用 program_b 以启动数据采集。

**program_b**：使用 K-LEB 工具针对指定 PID 进行性能数据采集，并将采集的数据存储在 ./kleb_pid/tmp 目录下的输出文件中。



**环境搭建**

```
git@github.com:Tech2Nova/kleb_pid.git

cd /K-LEB-Intel-demo

sudo bash initialize.sh

select 2
```

**快速开始**

```bash
cd /kleb_pid

make clean
make

sudo ./program_a_user

```

