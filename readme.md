# 基于智能体和进程捕获的恶意软件检测模型

## 项目概述

本项目实现了一个基于 Linux eBPF（Extended Berkeley Packet Filter）的系统调用监控工具，结合性能事件采集和基于深度强化学习（DQN, Deep Q-Network）智能体的神经网络推理，用于实时检测进程的恶意行为。项目通过跟踪 `execve` 系统调用捕获新进程的启动，收集进程的硬件性能计数器数据（如指令数、CPU 周期等），并利用预训练的 DQN 智能体对进程行为进行分类，判断其是否为恶意进程。使用强化学习技术使得模型在动态环境中更具适应性和鲁棒性，突显了项目的技术新颖性。

## 功能

1. **eBPF 程序**：监控 `execve` 系统调用，捕获新进程的 PID。
2. **性能事件采集**：为每个捕获的进程收集硬件性能计数器数据（如指令数、CPU 周期、分支指令、分支预测失败）。
3. **深度强化学习推理**：通过 DQN 智能体对性能数据进行推理，判断进程是“良性”还是“恶意”。
4. **多线程架构**：使用多线程处理性能数据采集和推理任务，确保高效性和实时性。
5. **数据管理**：通过哈希表管理进程数据，自动清理过期数据以优化内存使用。

## 文件结构

- **`program_a_bpf.c`**：eBPF 程序，负责捕获 `execve` 系统调用并将 PID 输出到用户态。
- **`the_main.c`**：主程序，加载并附加 eBPF 程序，处理性能事件，创建监控线程。
- **`receive.c`**：接收线程，处理性能数据，执行 DQN 神经网络推理，输出分类结果。
- **`collect.c`**：性能事件采集模块，收集硬件性能计数器数据并通过管道传递。
- **`train.py`**：基于 PyTorch 和 DQN 的模型训练脚本，生成神经网络权重文件 `model_weights.bin`。
- **`makefile`**：自动化编译脚本，简化项目构建流程。

## 工作流程

1. **eBPF 监控：**program_a_bpf.c 使用 tracepoint 跟踪 execve 系统调用，获取进程 PID 并通过 BPF_MAP_TYPE_PERF_EVENT_ARRAY输出到用户态。

   ```c
   SEC("tracepoint/syscalls/sys_enter_execve")
   int trace_execve(struct trace_event_raw_sys_enter *ctx) {
       u32 pid = bpf_get_current_pid_tgid() >> 32;
       bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &pid, sizeof(pid));
       return 0;
   }
   ```

2. **主程序初始化**：`the_main.c` 加载 eBPF 程序，设置信号处理，创建性能事件缓冲区，并启动接收线程。

3. **性能数据采集**：collect.c 使用 Linux perf_event_open 系统调用收集目标进程的性能数据，每 10 次采样通过管道发送格式化数据。

   ```c
   struct perf_event_attr attr = create_event_attr(types[i], configs[i]);
   fds[i] = syscall(__NR_perf_event_open, &attr, target_pid, -1, -1, 0);
   ```
   
4. **数据接收与推理：**receive.c 从管道接收数据，存储到哈希表中，并使用 DQN 神经网络模型进行推理。模型包含三层全连接网络（输入 40，隐藏层 128 和 64，输出 2），使用 ReLU 激活函数。

   ```c
   void forward(float* input, float* output) {
       float hidden1[HIDDEN1_DIM];
       float hidden2[HIDDEN2_DIM];
       matmul(fc1_weight, input, hidden1, HIDDEN1_DIM, INPUT_DIM);
       // ... ReLU 和后续层计算
   }
   ```

5. **结果输出**：推理结果以“良性”或“恶意”标签输出，并包含 PID 和数据条目数。

## 模型训练

模型训练通过 `train.py` 脚本使用深度强化学习（DQN）实现，显著提高了模型在复杂环境下的泛化能力。训练过程如下：

1. **数据加载：**从 dataset/benign/benign_vec 和 dataset/ransomware/ransomware_vec 加载 CSV 数据，每 10 行数据组成一个 40 维向量。

   ```python
   def load_data(directory, label):
       data = []
       for filename in os.listdir(directory):
           if filename.endswith('.csv'):
               filepath = os.path.join(directory, filename)
               df = pd.read_csv(filepath)
               for start in range(0, len(df), 10):
                   chunk = df.iloc[start:start+10].values.flatten()
                   if len(chunk) == 40:
                       data.append(chunk)
       return data, [label] * len(data)
   ```

2. **DQN 智能体：**使用 PyTorch 实现 DQN 模型，包含三层全连接网络（40→128→64→2）。智能体通过 ε-贪心策略选择动作（良性或恶意），并利用经验回放优化模型。

   ```python
   class DQN(nn.Module):
       def __init__(self, input_dim, output_dim):
           super(DQN, self).__init__()
           self.fc1 = nn.Linear(input_dim, 128)
           self.fc2 = nn.Linear(128, 64)
           self.fc3 = nn.Linear(64, output_dim)
   ```

3. **训练与保存：**训练 8000 回合，保存模型权重到 model.pth（PyTorch 格式）和 model_weights.bin（二进制格式，供 C 程序推理使用）。

   ```python
   torch.save(agent.policy_net.state_dict(), MODEL_PATH)
   weights = np.concatenate([param.cpu().detach().numpy().flatten() for param in agent.policy_net.parameters()])
   weights.tofile(WEIGHTS_PATH)
   ```

4. **测试**：在测试集上评估模型准确率，确保模型性能。

## 关键特性

- **高效性**：使用 eBPF 实现低开销的系统调用监控。
- **实时性**：通过多线程和管道机制实现实时数据处理和推理。
- **强化学习**：采用 DQN 智能体，增强模型在动态环境中的适应性和检测准确性。
- **数据管理**：哈希表存储进程数据，自动清理超过 10 秒的旧数据。
- **模型推理**：加载预训练的 `model_weights.bin`，对性能数据进行分类。
- **自动化构建**：提供 `makefile`，一键编译项目。

## 编译与运行

1. 训练模型：

   ```bash
   python3 train.py
   ```

   确保数据集路径正确，生成 model_weights.bin。

   

2. 编译项目：

   ```bash
   make
   ```

   使用 makefile 自动编译 eBPF 程序和用户态程序，生成可执行文件 the_main。

   

3. 运行程序：

   ```bash
   sudo ./the_main
   ```

   需要 sudo 权限以加载 eBPF 程序和访问性能计数器。

   

4. 清理：

   ```bash
   make clean
   ```

   删除生成的目标文件和可执行文件。

## 使用说明

- 程序启动后会监控所有 `execve` 系统调用，输出捕获的 PID 和推理结果。

- 按 `Ctrl+C` 退出程序，程序会清理哈希表和管道资源。

- 确保 `model_weights.bin` 文件存在于工作目录。

- 输出示例：

  ```
  [execve] Caught process PID: 29399
  Received raw data:
  [PID: 29399] Samples 0–9:
  Event: instructions        
    [00] 30927919	  [01] 24655709	  [02] 34711965	  [03] 24655709	  [04] 35138758	  [05] 24655709	  [06] 35138758	  [07] 24655709	  [08] 35138758	  [09] 24655709	
  Event: cycles              
    [00] 27140315	  [01] 20412593	  [02] 32751861	  [03] 20412593	  [04] 33553536	  [05] 20412593	  [06] 33553536	  [07] 20412593	  [08] 33553536	  [09] 20412593	
  Event: branch-instructions 
    [00] 5822132	  [01] 5131340	  [02] 6610234	  [03] 5131340	  [04] 6699073	  [05] 5131340	  [06] 6699073	  [07] 5131340	  [08] 6699073	  [09] 5131340	
  Event: branch-misses       
    [00] 83885	  [01] 83567	  [02] 112199	  [03] 83567	  [04] 116081	  [05] 83567	  [06] 116081	  [07] 83567	  [08] 116081	  [09] 83567	
  
  PID 29399 推理结果 (第 1 次接收): 恶意 (0=良性, 1=恶意, 预测值=1)
  Stored data for PID 29399, total entries: 1
  
  [PID: 29399] Samples 10–19:
  Event: instructions        
    [10] 35138758	  [11] 24655709	  [12] 35138758	  [13] 24655709	  [14] 35138758	  [15] 24655709	  [16] 35138758	  [17] 24655709	  [18] 35138758	  [19] 24655709	
  Event: cycles              
    [10] 33553536	  [11] 20412593	  [12] 33553536	  [13] 20412593	  [14] 33553536	  [15] 20412593	  [16] 33553536	  [17] 20412593	  [18] 33553536	  [19] 20412593	
  Event: branch-instructions 
    [10] 6699073	  [11] 5131340	  [12] 6699073	  [13] 5131340	  [14] 6699073	  [15] 5131340	  [16] 6699073	  [17] 5131340	  [18] 6699073	  [19] 5131340	
  Event: branch-misses       
    [10] 116081	  [11] 83567	  [12] 116081	  [13] 83567	  [14] 116081	  [15] 83567	  [16] 116081	  [17] 83567	  [18] 116081	  [19] 83567	
  
  PID 29399 推理结果 (第 2 次接收): 恶意 (0=良性, 1=恶意, 预测值=1)
  Stored data for PID 29399, total entries: 2
  
  [PID: 29399] Samples 20–29:
  Event: instructions        
    [20] 35138758	  [21] 24655709	  [22] 35138758	  [23] 24655709	  [24] 35138758	  [25] 24655709	  [26] 35138758	  [27] 24655709	  [28] 35138758	  [29] 24655709	
  Event: cycles              
    [20] 33553536	  [21] 20412593	  [22] 33553536	  [23] 20412593	  [24] 33553536	  [25] 20412593	  [26] 33553536	  [27] 20412593	  [28] 33553536	  [29] 20412593	
  Event: branch-instructions 
    [20] 6699073	  [21] 5131340	  [22] 6699073	  [23] 5131340	  [24] 6699073	  [25] 5131340	  [26] 6699073	  [27] 5131340	  [28] 6699073	  [29] 5131340	
  Event: branch-misses       
    [20] 116081	  [21] 83567	  [22] 116081	  [23] 83567	  [24] 116081	  [25] 83567	  [26] 116081	  [27] 83567	  [28] 116081	  [29] 83567	
  
  PID 29399 推理结果 (第 3 次接收): 恶意 (0=良性, 1=恶意, 预测值=1)
  Stored data for PID 29399, total entries: 3
  
  ```

## Attention事项

- **权限**：运行需要 root 权限以加载 eBPF 程序和访问性能计数器。
- **模型权重**：`model_weights.bin` 必须与神经网络结构匹配（输入 40，隐藏层 128 和 64，输出 2）。
- **数据清理**：哈希表会自动清理超过 10 秒的进程数据。
- **性能开销**：性能计数器采样频率为每 10 毫秒一次，可调整 `SAMPLE_INTERVAL_MS`。

## 未来改进

- 支持更多性能事件类型。
- 优化 DQN 模型以提高推理准确性和训练效率。
- 添加可视化界面以展示监控结果。
