// SystemOptimizer.cpp
#include "SystemOptimizer.h"
#include <spdlog/spdlog.h>
#include <sys/sysinfo.h>
#include <memory>
#include <iomanip>

// 外部定义的logger（在main.cpp中定义）
extern std::shared_ptr<spdlog::logger> logger;

bool SystemOptimizer::optimizeForHighConcurrency() {
    logger->info("🔧 开始应用程序级系统优化...");
    std::cout << "🔧 应用程序级系统优化..." << std::endl;

    // 显示当前用户信息
    int userId = getuid();
    logger->info("当前用户ID: {}", userId);
    std::cout << "当前用户ID: " << userId << std::endl;

    bool all_success = true;
    bool needs_optimization = false;

    // 1. 设置进程文件描述符限制
    if (!setFileDescriptorLimit()) {
        logger->error("文件描述符限制设置失败");
        std::cerr << "❌ 文件描述符限制设置失败" << std::endl;
        all_success = false;
        needs_optimization = true;
    }

    // 2. 显示系统信息
    printSystemInfo();

    // 3. 检查TCP参数优化状态
    if (!checkTCPOptimization()) {
        needs_optimization = true;
    }

    // 4. 只在需要优化时才显示建议
    if (needs_optimization) {
        printRecommendations();
    }
    else {
        std::cout << "✅ 系统状态良好，无需额外优化" << std::endl;
        logger->info("系统状态良好，无需额外优化");
    }

    if (all_success && !needs_optimization) {
        logger->info("系统优化检查完成 - 状态良好");
    }
    else if (all_success) {
        logger->info("系统优化检查完成 - 有优化建议");
    }
    else {
        logger->warn("系统优化检查完成 - 存在警告");
    }

    return all_success;
}

void SystemOptimizer::printCurrentLimits() {
    struct rlimit rl;

    logger->info("检查当前进程限制...");
    std::cout << "\n📊 当前进程限制:" << std::endl;

    // 文件描述符
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        std::string limit_info = "文件描述符: " + std::to_string(rl.rlim_cur) +
            " (软限制) / " + std::to_string(rl.rlim_max) + " (硬限制)";
        logger->info(limit_info);
        std::cout << "  " << limit_info << std::endl;
    }
    else {
        logger->error("获取文件描述符限制失败: {}", strerror(errno));
        perror("获取文件描述符限制失败");
    }

    // 栈大小
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        long soft_limit_mb = rl.rlim_cur / 1024 / 1024;
        long hard_limit_mb = rl.rlim_max / 1024 / 1024;
        std::string stack_info = "栈大小: " + std::to_string(soft_limit_mb) +
            "MB (软限制) / " + std::to_string(hard_limit_mb) + "MB (硬限制)";
        logger->info(stack_info);
        std::cout << "  " << stack_info << std::endl;
    }
}

bool SystemOptimizer::setFileDescriptorLimit() {
    struct rlimit rl;

    // 获取当前限制
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        logger->error("获取文件描述符限制失败: {}", strerror(errno));
        perror("❌ 获取文件描述符限制失败");
        return false;
    }

    std::string current_info = "当前文件描述符限制: " + std::to_string(rl.rlim_cur) +
        " (软限制) / " + std::to_string(rl.rlim_max) + " (硬限制)";
    logger->info(current_info);
    std::cout << current_info << std::endl;

    // 检查是否已经优化
    bool is_optimized = (rl.rlim_cur >= 100000);

    if (is_optimized) {
        logger->info("当前文件描述符限制已足够");
        std::cout << "✅ 当前文件描述符限制已足够" << std::endl;
        return true;
    }

    // 尝试设置新的限制
    rlimit new_rl = rl;
    new_rl.rlim_cur = 100000;  // 软限制：10万
    new_rl.rlim_max = 1000000; // 硬限制：100万

    if (setrlimit(RLIMIT_NOFILE, &new_rl) != 0) {
        logger->warn("无法设置文件描述符限制: {} (当前用户权限可能不足)", strerror(errno));
        std::cout << "⚠️  无法设置文件描述符限制，当前用户权限可能不足" << std::endl;
        return false;
    }

    // 验证设置
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        std::string new_info = "文件描述符限制已设置为: " + std::to_string(rl.rlim_cur) +
            " (软限制) / " + std::to_string(rl.rlim_max) + " (硬限制)";
        logger->info(new_info);
        std::cout << "✅ " << new_info << std::endl;
        return true;
    }

    logger->error("验证文件描述符限制设置失败");
    return false;
}

bool SystemOptimizer::checkTCPOptimization() {
    logger->info("检查TCP参数优化状态...");

    bool all_optimized = true;

    // 检查关键TCP参数
    std::vector<std::pair<std::string, long>> tcp_params = {
        {"net.core.somaxconn", 65535},
        {"net.ipv4.tcp_max_syn_backlog", 65535}
    };

    for (const auto& param : tcp_params) {
        std::string command = "sysctl -n " + param.first + " 2>/dev/null";
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                long current_value = std::atol(buffer);
                pclose(pipe);

                if (current_value < param.second) {
                    logger->warn("TCP参数 {} 未优化: 当前值={}, 推荐值={}",
                        param.first, current_value, param.second);
                    std::cout << "⚠️  TCP参数 " << param.first << " 未优化: "
                        << current_value << " (当前) < " << param.second << " (推荐)" << std::endl;
                    all_optimized = false;
                }
                else {
                    logger->info("TCP参数 {} 已优化: {}", param.first, current_value);
                }
            }
            else {
                pclose(pipe);
                logger->warn("无法读取TCP参数: {}", param.first);
                all_optimized = false;
            }
        }
        else {
            logger->warn("无法检查TCP参数: {}", param.first);
            all_optimized = false;
        }
    }

    if (all_optimized) {
        std::cout << "✅ TCP参数已优化" << std::endl;
        logger->info("TCP参数检查完成 - 已优化");
    }
    else {
        std::cout << "❌ 部分TCP参数需要优化" << std::endl;
        logger->info("TCP参数检查完成 - 需要优化");
    }

    return all_optimized;
}

void SystemOptimizer::printSystemInfo() {
    logger->info("收集系统信息...");
    std::cout << "\n💻 系统信息:" << std::endl;

    // 获取CPU核心数
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    logger->info("CPU核心数: {}", cores);
    std::cout << "  CPU核心数: " << cores << std::endl;

    // 获取系统内存
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        long total_memory_mb = (si.totalram * si.mem_unit) / 1024 / 1024;
        long free_memory_mb = (si.freeram * si.mem_unit) / 1024 / 1024;
        long used_memory_mb = total_memory_mb - free_memory_mb;
        double memory_usage = (double)used_memory_mb / total_memory_mb * 100.0;

        logger->info("系统内存: {} MB (总) / {} MB (可用) / {:.1f}% 使用率",
            total_memory_mb, free_memory_mb, memory_usage);
        std::cout << "  总内存: " << total_memory_mb << " MB" << std::endl;
        std::cout << "  可用内存: " << free_memory_mb << " MB" << std::endl;
        std::cout << "  内存使用率: " << std::fixed << std::setprecision(1) << memory_usage << "%" << std::endl;

        // 检查负载平均值
        double load_1min = (double)si.loads[0] / (1 << SI_LOAD_SHIFT);
        double load_5min = (double)si.loads[1] / (1 << SI_LOAD_SHIFT);
        double load_15min = (double)si.loads[2] / (1 << SI_LOAD_SHIFT);

        logger->info("系统负载: {:.2f} (1min) / {:.2f} (5min) / {:.2f} (15min)",
            load_1min, load_5min, load_15min);
        std::cout << "  系统负载: " << std::fixed << std::setprecision(2)
            << load_1min << " (1min) / " << load_5min << " (5min) / "
            << load_15min << " (15min)" << std::endl;
    }
    else {
        logger->warn("获取系统内存信息失败: {}", strerror(errno));
        std::cout << "  内存信息: 获取失败" << std::endl;
    }

    // 检查是否在容器中
    if (isInContainer()) {
        logger->info("运行环境: 容器");
        std::cout << "  环境: 容器" << std::endl;
    }
    else {
        logger->info("运行环境: 物理机/虚拟机");
        std::cout << "  环境: 物理机/虚拟机" << std::endl;
    }
}

bool SystemOptimizer::isInContainer() {
    // 检查是否在Docker容器中
    std::ifstream cgroup("/proc/1/cgroup");
    if (!cgroup.is_open()) {
        logger->debug("无法打开/proc/1/cgroup，假设不在容器中");
        return false;
    }

    std::string line;
    while (std::getline(cgroup, line)) {
        if (line.find("docker") != std::string::npos ||
            line.find("kubepods") != std::string::npos ||
            line.find("containerd") != std::string::npos) {
            logger->debug("检测到容器环境: {}", line);
            return true;
        }
    }

    // 检查/.dockerenv文件（Docker的另一个标志）
    std::ifstream dockerenv("/.dockerenv");
    if (dockerenv.is_open()) {
        logger->debug("检测到/.dockerenv文件，在容器环境中");
        return true;
    }

    return false;
}

void SystemOptimizer::printRecommendations() {
    logger->info("生成优化建议...");
    std::cout << "\n💡 优化建议:" << std::endl;

    if (getuid() != 0) {
        logger->info("建议使用root权限运行以获得完整优化效果");
        std::cout << "  1. 使用sudo运行以获得完整优化效果" << std::endl;
    }

    std::cout << "  2. 执行系统优化脚本: sudo ./optimize_system.sh" << std::endl;
    std::cout << "  3. 对于生产环境，建议重启系统" << std::endl;
    std::cout << "  4. 监控系统资源命令:" << std::endl;
    std::cout << "     - 连接监控: watch -n 1 'netstat -an | grep :9999 | wc -l'" << std::endl;
    std::cout << "     - 内存监控: watch -n 1 'free -h'" << std::endl;
    std::cout << "     - 进程监控: watch -n 1 'ps aux | grep tetris_server'" << std::endl;

    logger->info("优化建议已显示");
}