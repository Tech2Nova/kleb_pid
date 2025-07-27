#### 编译和运行

1. 编译：

   ```bash
   make clean 
   make
   ```

   赋予脚本执行权限：

   ```bash
   chmod +x run_samples.sh
   ```

2. 运行脚本：

   ```bash
   ./run_samples.sh
   ```



#### 输出目录结构

假设样本 1.bin 生成了两个 PID（1234 和 1235），数据目录结构如下：

```
data/
├── data_a_1/
│   ├── perf_output_1234.csv
│   ├── perf_output_1235.csv
│   ├── usage_20250725_195330.csv
├── data_a_2/
│   ├── perf_output_5678.csv
│   ├── usage_20250725_195331.csv
...
├── data_a_10000/
```

- 性能计数器文件（例如，data_a_1/perf_output_1234.csv）：

  ```
  sample,branches,cache-references,cache-misses,bus-cycles
  0,0,0,0,0
  1,123456,7890,234,567
  ...
  9,125000,8000,250,580
  ```

- 性能监控文件（例如，data_a_1/usage_20250725_195330.csv）：

  ```text
  Timestamp,CPU(%),RAM(KB),VirtualMem(KB)
  2025-07-25 19:53:30.123,2.5,10240,20480
  ...
  ```

  