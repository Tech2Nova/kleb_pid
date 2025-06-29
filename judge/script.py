import os

def truncate_csv_files():
    # 定义目标目录路径
    target_dir = os.path.join('dataset', 'benign', 'test_data')
    
    # 遍历目标目录下的所有文件
    for filename in os.listdir(target_dir):
        if filename.endswith('.csv'):
            filepath = os.path.join(target_dir, filename)
            
            # 读取文件内容
            with open(filepath, 'r') as f:
                lines = f.readlines()
            
            # 检查文件行数是否足够
            if len(lines) > 90:  # 包括标题行总共超过90行
                # 保留标题行和前90行数据（共91行）
                truncated_lines = lines[:91]
            elif len(lines) > 1:  # 文件行数不足但至少有数据
                # 保留所有行（不删除）
                truncated_lines = lines
            else:  # 空文件或只有标题行
                truncated_lines = lines
            
            # 写回文件
            with open(filepath, 'w') as f:
                f.writelines(truncated_lines)
            
            print(f"Processed {filename}: original {len(lines)} lines, kept {len(truncated_lines)} lines")

if __name__ == "__main__":
    truncate_csv_files()
    print("All CSV files in dataset\\benign\\benign_vec processed.")