#!/bin/bash

# 脚本：run_samples.sh
# 用途：依次执行 sample/ 目录下的样本，运行 collect 程序采集 HPC 数据

SAMPLE_DIR="sample"
DATA_BASE_DIR="data"
COLLECT_PROG="./collect"
TOTAL_SAMPLES=10000
WAIT_TIME_MS=105000 # 105ms，转换为微秒

# 检查 sample 目录是否存在
if [ ! -d "$SAMPLE_DIR" ]; then
    echo "错误：样本目录 $SAMPLE_DIR 不存在"
    exit 1
fi

# 检查 collect 程序是否存在
if [ ! -x "$COLLECT_PROG" ]; then
    echo "错误：collect 程序 $COLLECT_PROG 不存在或不可执行"
    exit 1
fi

# 创建数据基目录
mkdir -p "$DATA_BASE_DIR" || {
    echo "错误：无法创建数据目录 $DATA_BASE_DIR"
    exit 1
}

for ((i=1; i<=TOTAL_SAMPLES; i++)); do
    SAMPLE_PATH="$SAMPLE_DIR/$i"
    SAMPLE_DATA_DIR="$DATA_BASE_DIR/data_a_$i"

    # 检查样本文件是否存在
    if [ ! -x "$SAMPLE_PATH" ]; then
        echo "警告：样本 $SAMPLE_PATH 不存在或不可执行，跳过"
        continue
    }

    echo "处理样本 $i: $SAMPLE_PATH"

    # 启动 collect 程序（需要 sudo）
    sudo $COLLECT_PROG "$SAMPLE_DATA_DIR" &
    COLLECT_PID=$!

    # 等待 collect 程序启动
    sleep 0.1

    # 执行样本
    "$SAMPLE_PATH" &

    # 等待 1050ms
    usleep $WAIT_TIME_MS

    # 终止 collect 程序
    sudo kill -SIGINT $COLLECT_PID 2>/dev/null
    wait $COLLECT_PID 2>/dev/null

    echo "样本 $i 完成，数据保存至 $SAMPLE_DATA_DIR"
done

echo "所有样本处理完成"
