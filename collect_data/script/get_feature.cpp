#include "head.h"
#include <iostream>
#include <thread>
#include <chrono>

#include <fstream>
#include <sstream>
#include <iterator>
#include <vector>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <sys/resource.h>

// 性能数据结构体
struct PerformanceData {
    std::chrono::system_clock::time_point timestamp;
    double cpu_usage;
    size_t ram_usage;    // KB
    size_t virtual_mem;  // KB
};

// 线程安全性能监控器
class PerformanceMonitor {
public:
    PerformanceMonitor(int interval_ms = 1000) 
        : interval(interval_ms), running(false) {}

    void start() {
        running = true;
        // 初始化前一次的 CPU 时间（第一次采样）
        auto initial_proc = get_process_cpu_time();
        auto initial_sys = get_system_cpu_time();
        prev_proc_time = initial_proc;
        prev_sys_time = initial_sys;

        monitor_thread = std::thread([this](){
            auto prev_proc_time = get_process_cpu_time();  // 线程内的前一次时间（局部变量，避免共享）
            auto prev_sys_time = get_system_cpu_time();

            while(running) {
                auto start = std::chrono::steady_clock::now();
                
                // 采集CPU
                auto curr_proc_time = get_process_cpu_time();
                auto curr_sys_time = get_system_cpu_time();
                double cpu = calculate_cpu_usage(prev_proc_time, curr_proc_time,
                                                prev_sys_time, curr_sys_time);
                
                // 采集内存
                auto mem = get_memory_usage();

                // 记录数据（加锁保护共享数据）
                {
                    std::lock_guard<std::mutex> lock(data_mutex);
                    data.push_back({
                        std::chrono::system_clock::now(),
                        cpu,
                        mem.resident,
                        mem.virtual_size
                    });
                }

                prev_proc_time = curr_proc_time;  // 更新线程内的前一次时间
                prev_sys_time = curr_sys_time;

                // 精确间隔控制
                auto end = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if(elapsed < interval) {
                    std::this_thread::sleep_for(interval - elapsed);
                }
            }
        });
    }

    void stop() {
        running = false;
        if(monitor_thread.joinable()) {
            monitor_thread.join();
        }
    }

    void save_csv(const std::string& filename) {
        std::lock_guard<std::mutex> lock(data_mutex);
        std::ofstream file(filename);
        file << "Timestamp,CPU(%),RAM(KB),VirtualMem(KB)\n";
        
        for(const auto& entry : data) {
            auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
            // 使用 localtime_r 替代 localtime 保证线程安全（Linux）
            struct tm* local_time = localtime(&time); 
            file << std::put_time(local_time, "%F %T") << ","
                 << entry.cpu_usage << ","
                 << entry.ram_usage << ","
                 << entry.virtual_mem << "\n";
        }
    }

    // 新增：获取最新性能数据（从已采集的 data 中返回）
    PerformanceData get_latest_performance_data() {
        std::lock_guard<std::mutex> lock(data_mutex);
        if (data.empty()) {
            return {std::chrono::system_clock::now(), 0.0, 0, 0}; // 默认值
        }
        return data.back();
    }

private:
    struct CpuTime { 
        unsigned long user, system; 
        unsigned long long total_system;
    };

    struct MemoryUsage { 
        size_t resident; 
        size_t virtual_size; 
    };

    // 获取进程CPU时间（用户态+内核态）
    CpuTime get_process_cpu_time() {
        std::ifstream stat("/proc/self/stat");
        std::string line;
        std::getline(stat, line);
        
        std::istringstream iss(line);
        std::vector<std::string> fields{
            std::istream_iterator<std::string>(iss),
            std::istream_iterator<std::string>()
        };

        // /proc/[pid]/stat 中 utime 是第14字段（索引13），stime 是第15字段（索引14）
        return {
            std::stoul(fields[13]),  // utime
            std::stoul(fields[14]),  // stime
            0
        };
    }

    // 获取系统总CPU时间
    CpuTime get_system_cpu_time() {
	std::ifstream stat("/proc/stat");
	    if (!stat.is_open()) {
		throw std::runtime_error("Failed to open /proc/stat");
	    }

	    std::string line;
	    if (!std::getline(stat, line)) {
		throw std::runtime_error("Failed to read /proc/stat");
	    }

	    std::istringstream iss(line);
	    std::vector<std::string> fields{
		std::istream_iterator<std::string>(iss),
		std::istream_iterator<std::string>()
	    };

	    // 检查字段数量是否足够（至少5个字段，索引0-4）
	    if (fields.size() < 5) {
		throw std::runtime_error("/proc/stat has insufficient fields (expected at least 5)");
	    }

	    // 正确解析 user（索引1）、nice（索引2）、system（索引3）、idle（索引4）
	    const std::string& user_str = fields[1];
	    const std::string& nice_str = fields[2];
	    const std::string& system_str = fields[3];
	    const std::string& idle_str = fields[4];

	    // 安全转换函数（带错误提示）
	    auto safe_stoul = [](const std::string& s, const std::string& field_name) -> unsigned long {
		if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) {
		    throw std::invalid_argument("Invalid value for " + field_name + ": " + s);
		}
		try {
		    return std::stoul(s);
		} catch (const std::out_of_range&) {
		    throw std::invalid_argument(field_name + " value out of range: " + s);
		}
	    };

	    unsigned long user = safe_stoul(user_str, "user");
	    unsigned long nice = safe_stoul(nice_str, "nice");
	    unsigned long system = safe_stoul(system_str, "system");
	    unsigned long idle = safe_stoul(idle_str, "idle");

	    return {
		0,
		0,
		user + nice + system + idle  // 总CPU时间 = user + nice + system + idle
	    };
    }

    // 计算CPU使用率（百分比）
    double calculate_cpu_usage(const CpuTime& prev_proc, const CpuTime& curr_proc,
                              const CpuTime& prev_sys, const CpuTime& curr_sys) {
        const unsigned long proc_diff = 
            (curr_proc.user + curr_proc.system) - 
            (prev_proc.user + prev_proc.system);
        
        const unsigned long long sys_diff = 
            curr_sys.total_system - prev_sys.total_system;

        return (sys_diff == 0) ? 0.0 : 
            (100.0 * proc_diff / sys_diff);
    }

    // 获取内存使用情况
    MemoryUsage get_memory_usage() {
        std::unordered_map<std::string, unsigned long> mem_info;

        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.find("VmRSS:") == 0) {  
                std::istringstream iss(line.substr(6));  
                iss >> mem_info["VmRSS"];            
            } else if (line.find("VmSize:") == 0) {    
                std::istringstream iss(line.substr(7));  
                iss >> mem_info["VmSize"];             
            }
        }

        return { 
            mem_info["VmRSS"], 
            mem_info["VmSize"]  
        };
    }

    // 新增：获取最新性能数据（从采集的 data 中返回）
    PerformanceData get_performance_data() {
        return get_latest_performance_data(); // 直接复用已有逻辑
    }

    std::thread monitor_thread;
    std::atomic<bool> running;
    std::chrono::milliseconds interval;
    std::vector<PerformanceData> data;
    std::mutex data_mutex;  // 保护 data 的互斥锁
    CpuTime prev_proc_time;  // 类成员变量，保存前一次进程CPU时间（需加锁）
    CpuTime prev_sys_time;   // 类成员变量，保存前一次系统CPU时间（需加锁）
};
PerformanceMonitor g_monitor;

int main(int argc, char* argv[]) {
    g_monitor.start();

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <project_name>\n";
        return 1;
    }

    // 读取要转储的进程名称
    std::string project_name = argv[1];
    // 关键数据结构 存储perfc相关的数据
    std::unordered_map<std::string, unsigned long long> PerfcMap;
    std::unordered_map<std::string, std::vector<unsigned long long>> PerfcSave;
    
    vmi_instance_t vmi;
    addr_t cr3 = 0;

    // 检查能否开启LIBVMI
    // 虚拟机的名称应该为win7-hvm，如果要修改虚拟机名称则修改第二个参数
    if(VMI_FAILURE == vmi_init_complete(&vmi, "win7-hvm", VMI_INIT_DOMAINNAME | VMI_INIT_EVENTS, NULL, VMI_CONFIG_GLOBAL_FILE_ENTRY, NULL, NULL)){
        printf("Failed to init libvmi library\n");
        return 0;
    }

    // 检查输入的进程名是否符合需求
    // 在我们的代码中，以_m结尾代表恶意软件，以_b结尾代表良性软件
    if(!check_project_name(project_name))
        return 0;

    // 补足进程名称，在进程列表中的进程名包含.exe后缀
    std::string full_project_name = project_name + ".exe";
    

    // 读取进程的页表基地址
    if(!get_cr3_by_projectname("win7-hvm", full_project_name, cr3, vmi)){
        printf("error to find the target process and cr3! exit \n");
        vmi_destroy(vmi);
        return 0;
    }

    // 依照进程名称构成保存数据的文件夹
    // 所有数据保存在 save 文件夹下，如果需要修改保存路径，请修改第二个参数
    std::string save_directory = create_save_directory(project_name, "save");
    if(save_directory.size() == 0){
        vmi_destroy(vmi);
        return 0;
    }

    // 创建一个线程 用于读取xen平台性能计数的数据，读取30秒
    std::thread thread1([&]{
        for (int seconds = 0; seconds < 13; ++seconds) {
            //std::cout << "Executing get_perf_message at mi_second: " << seconds << std::endl;
            get_perf_message(PerfcMap, PerfcSave);
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    });

    // 30秒后执行 get_memory_by_cr3， 转储进程内存
    std::thread thread2([&]{
        addr_t cr3_bak = 0;
        std::this_thread::sleep_for(std::chrono::seconds(30));
        get_cr3_by_projectname("win7-hvm", full_project_name, cr3_bak, vmi);
        std::cout << "Executing get_memory_by_projectname" << std::endl;
        get_memory_by_cr3("win7-hvm", save_directory, cr3, vmi);
    });

    // 60秒后执行 printMapToFile，讲读取的Xen平台性能计数的数据保存到文本中，
    std::thread thread3([&]{
        std::this_thread::sleep_for(std::chrono::seconds(68));
        std::cout << "Executing printMapToFile" << std::endl;
        printMapToFile(PerfcSave, save_directory);
    });

    // 执行线程s
    thread1.join();
    thread2.join();
    thread3.join();

    // 关闭vmi
    vmi_destroy(vmi);

    g_monitor.stop();
    std::string filename = argv[1];
    g_monitor.save_csv("/media/ym/MyPassport/project5/output/usage/" + filename + ".csv");
    return 0;
}
