#### **代码结构**

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

#### 程序功能

- **program_a_bpf**：内核层模块，负责捕获 execve 系统调用事件，提取进程 ID（PID）并传递至 program_a_user.c。
- **program_a_user**：用户空间程序，接收 program_a_bpf 传递的 PID，并针对每个 PID 调用 program_b 执行数据采集。
- **program_b**：利用 K-LEB 工具对指定 PID 进行性能数据采集，采集结果存储于 /tmp 目录下的输出文件中。



#### 附加功能

- **进程白名单**：为避免程序自身进程的干扰，系统为程序触发的进程建立白名单，仅监控外部目标进程。
- **重复 PID 过滤**：对同一进程在 5 秒内多次触发execve调用的pid 建立白名单，减少冗余 PID 的处理。



#### **环境搭建**

```
git@github.com:Tech2Nova/kleb_pid.git

cd /K-LEB-Intel-demo

sudo bash initialize.sh

select 2
```



#### **快速开始**

```bash
cd /kleb_pid

make clean
make

sudo ./program_a_user

```



#### **参数修改**

```c
1、 可以在program_b.c 32行处可以修改触发事件
	SEC("tracepoint/syscalls/sys_enter_execve")
    
2、可以在program_a_user.c 20行处修改pid捕获限制
    // PID 风暴检测参数
    #define EVENT_THRESHOLD 20
    #define TIME_WINDOW_NS 1000000L // 1ms（纳秒）
    
3、可以在program_a_user.c 91行处修改进程限制数量
     static int active_children = 0;
    #define MAX_CHILDREN 5
    if (active_children >= MAX_CHILDREN) {
        fprintf(stderr, "子进程数量过多，等待...\n");
        wait(NULL);
        active_children--;
    }

4、可以在program_b.c 33行处修改K-Leb命令
    snprintf(cmd, sizeof(cmd), 
             "sudo /home/mz2/Desktop/dcx/K-LEB-Intel-demo/ioctl_start -e BR_RET,BR_MISP_RET,LOAD,STORE -t 1 -o %s %d",
             log_path, pid);
```



#### 注意事项

- **K-LEB 工具部署**：使用前需确保 K-LEB 工具已正确部署，以支持程序运行。
- **代码修改与编译**：修改 /kleb_pid/code 文件后，需运行 make 命令重新编译，确保代码更新生效。



#### 未解决问题

1. **过多 PID 触发导致死锁**：当前系统仍可能因触发过多进程 ID（PID）而导致计算机死锁，需要重启电脑。
2. **HPC 数据量不足**：部分进程存活时间过短，采集的 HPC数据量不足，影响数据分析的完整性。
