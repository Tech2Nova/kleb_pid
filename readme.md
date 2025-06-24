**仅执行打开命令行一个操作，就返回了10个pid**

```bash
(base) mz2@mz2-Precision-3530:/tmp$ cat execve_log.txt 
Starting execve log
PID: 27089, Program: gnome-shell
PID: 27089, Program: sh
PID: 27089, Program: sh
PID: 27089, Program: sh
PID: 27089, Program: sh
PID: 27089, Program: sh
PID: 27091, Program: gnome-terminal
PID: 27096, Program: gnome-terminal-
PID: 27098, Program: bash
PID: 27099, Program: lesspipe
PID: 27101, Program: lesspipe
PID: 27102, Program: bash
PID: 27106, Program: bash
PID: 27108, Program: bash
PID: 27107, Program: bash
PID: 27110, Program: bash
PID: 27119, Program: (xtract-3)
```



**一个进程可能引发多次execve调用，导致重复返回pid**

```
(base) mz2@mz2-Precision-3530:~/Desktop/dcx/code$ sudo ./build/program_a_user 
[sudo] password for mz2: 
libbpf: Error in bpf_create_map_xattr(events):ERROR: strerror_r(-524)=22(-524). Retrying without BTF.
Program A is running. Press Ctrl+C to stop...
Event count reset: 0
Event count: 1
[execve] Caught process PID: 28968, Program: gnome-shell
Event count: 2
[execve] Caught process PID: 28968, Program: sh
Event count: 3
[execve] Caught process PID: 28968, Program: sh
Event count: 4
[execve] Caught process PID: 28968, Program: sh
Too many child processes: 3
Event count: 5
[execve] Caught process PID: 28968, Program: sh
Too many child processes: 3
Event count: 6
PID storm detected! (6 events in 1 second)
Exiting.

```



**针对上述两个问题，需要添加两个名单，一个是记录自身程序的pid，一个是记录接收过的pid号（5s清空一次）**



```bash
(base) mz2@mz2-Precision-3530:~/Desktop/dcx/code$ sudo ./build/program_a_user 
libbpf: Error in bpf_create_map_xattr(events):ERROR: strerror_r(-524)=22(-524). Retrying without BTF.
Program A is running. Press Ctrl+C to stop...
Event count reset: 0
Event count: 1
[execve] Caught process PID: 29491, Program: gnome-shell
Event count: 2
[execve] Caught process PID: 29496, Program: gnome-terminal
Event count: 3
[execve] Caught process PID: 29503, Program: gnome-terminal-
Event count: 4
[execve] Caught process PID: 29507, Program: bash
Event count: 5
[execve] Caught process PID: 29510, Program: lesspipe
Event count: 6
[execve] Caught process PID: 29514, Program: lesspipe
Too many child processes: 3
Event count: 7
[execve] Caught process PID: 29516, Program: bash
Too many child processes: 3
Event count: 8
[execve] Caught process PID: 29518, Program: bash
Too many child processes: 3
Event count: 9
[execve] Caught process PID: 29521, Program: bash
Event count: 10
[execve] Caught process PID: 29520, Program: bash
Event count: 11
[execve] Caught process PID: 29527, Program: bash
Event count reset: 11
Event count: 1
[execve] Caught process PID: 29538, Program: gnome-shell
Event count: 2
[execve] Caught process PID: 29557, Program: snap
Event count: 3
[execve] Caught process PID: 29565, Program: snap
Event count: 4
[execve] Caught process PID: 29568, Program: snap
Too many child processes: 3
Event count: 5
[execve] Caught process PID: 29569, Program: snap
Too many child processes: 3
Event count: 6
[execve] Caught process PID: 29570, Program: snap
Too many child processes: 3
Event count: 7
[execve] Caught process PID: 29584, Program: desktop-launch
Too many child processes: 3
Event count: 8
[execve] Caught process PID: 29585, Program: desktop-launch
Too many child processes: 3
Event count: 9
[execve] Caught process PID: 29587, Program: desktop-launch
Too many child processes: 3
Event count: 10
[execve] Caught process PID: 29588, Program: desktop-launch
Too many child processes: 3
Event count: 11
[execve] Caught process PID: 29590, Program: desktop-launch
Too many child processes: 3
Event count: 12
[execve] Caught process PID: 29591, Program: desktop-launch
Too many child processes: 3
Event count: 13
[execve] Caught process PID: 29592, Program: desktop-launch
Too many child processes: 3
Event count: 14
[execve] Caught process PID: 29593, Program: desktop-launch
Too many child processes: 3
Event count: 15
[execve] Caught process PID: 29598, Program: desktop-launch
Too many child processes: 3
Event count: 16
[execve] Caught process PID: 29603, Program: desktop-launch
Too many child processes: 3
Event count: 17
[execve] Caught process PID: 29604, Program: desktop-launch
Too many child processes: 3
Event count: 18
[execve] Caught process PID: 29605, Program: desktop-launch
Too many child processes: 3
Event count: 19
[execve] Caught process PID: 29606, Program: desktop-launch
Too many child processes: 3
Event count: 20
[execve] Caught process PID: 29607, Program: desktop-launch
Too many child processes: 3
Event count: 21
PID storm detected! (21 events in 1 second)
Exiting.
(base) mz2@mz2-Precision-3530:~/Desktop/dcx/code$ 

```











