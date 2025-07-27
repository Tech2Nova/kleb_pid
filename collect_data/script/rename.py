import os

def main():
    input_dir = "/media/mz2/HP USB321FD/linux"  # 修改为你的实际路径
    output_file = "md5_mapping.txt"

    if not os.path.isdir(input_dir):
        print(f"目录 {input_dir} 不存在！")
        return

    files = [f for f in os.listdir(input_dir) if os.path.isfile(os.path.join(input_dir, f))]
    files.sort()  # 保证顺序一致

    with open(output_file, "w", encoding="utf-8") as out_f:
        out_f.write("原文件名,新文件名\n")  # 写表头

        for index, old_name in enumerate(files, 1):
            old_path = os.path.join(input_dir, old_name)
            new_name = str(index)
            new_path = os.path.join(input_dir, new_name)

            try:
                os.rename(old_path, new_path)
                out_f.write(f"{old_name},{new_name}\n")
                print(f"重命名: {old_name} -> {new_name}")
            except Exception as e:
                print(f"重命名 {old_name} 时出错: {str(e)}")

if __name__ == "__main__":
    main()

