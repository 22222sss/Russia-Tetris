#include "ConnectionPool.h"

#include <chrono>
#include <cstdlib>
#include <deque>

#if defined(__GLIBC__)
#include <malloc.h>   // malloc_trim：把空闲页主动还给内核（见 trimIdleObjects）
#endif

extern shared_ptr<spdlog::logger> logger;

namespace {

// 读一个非负整数环境变量；缺省/非法时返回 fallback
long long envInt(const char* name, long long fallback) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const long long value = std::strtoll(raw, &end, 10);
    if (end == nullptr || *end != '\0' || value < 0) {
        return fallback;
    }
    return value;
}

}  // namespace

// 初始化静态成员
std::queue<std::shared_ptr<User>> ConnectionPool::pool;
std::mutex ConnectionPool::pool_mutex;
std::atomic<size_t> ConnectionPool::pool_size(0);
std::atomic<long> ConnectionPool::total_acquired(0);
std::atomic<long> ConnectionPool::total_released(0);
std::atomic<long> ConnectionPool::create_count(0);
std::atomic<long> ConnectionPool::peak_usage(0);
std::atomic<long> ConnectionPool::allocation_failures(0);
std::atomic<long> ConnectionPool::total_trimmed(0);

void ConnectionPool::initialize(size_t initial_size) {
    std::lock_guard<std::mutex> lock(pool_mutex);  // 线程安全

    logger->info("🔄 初始化连接池，预创建 {} 个User对象...", initial_size);

    for (size_t i = 0; i < initial_size; ++i) {
        // 创建User对象，fd设为-1表示"未分配状态"
        auto user = std::shared_ptr<User>(new User(-1));
        pool.push(user);
        pool_size++;
    }

    logger->info("✅ 连接池初始化完成，当前大小: {}", pool_size.load());
}

std::shared_ptr<User> ConnectionPool::acquire(int fd) {
    std::lock_guard<std::mutex> lock(pool_mutex);  // 线程安全
    total_acquired++;  // 原子递增

    std::shared_ptr<User> user;

    if (!pool.empty()) {
        // ✅ 池中有可用对象 - 复用
        user = pool.front();  // 获取队列头部的对象
        pool.pop();           // 从队列移除
        pool_size--;          // 更新池大小

        // 🔧 重置对象状态（重要！）
        user->resetUserInfo();    // 清除之前的状态
        user->setFd(fd);          // 设置新的文件描述符
        user->setStatus(STATUS_NOTSTART);  // 设置为初始状态

        logger->debug("从连接池获取User对象, fd: {}, 池剩余: {}",
            fd, pool_size.load());
    }
    else {
        // ⚠️ 池为空 - 创建新对象
        user = std::shared_ptr<User>(new User(fd));
        create_count++;  // 记录池外创建次数

        logger->debug("连接池为空，创建新User对象, fd: {}, 总创建数: {}",
            fd, create_count.load());
    }

    return user;
}

void ConnectionPool::release(std::shared_ptr<User> user) {
    if (!user) {
        logger->warn("尝试释放空的User对象");
        return;
    }

    std::lock_guard<std::mutex> lock(pool_mutex);  // 线程安全
    total_released++;  // 原子递增

    if (pool_size < MAX_POOL_SIZE) {
        // ✅ 池未满 - 回收对象

        // 🔧 重置对象状态
        user->resetUserInfo();
        user->setFd(-1);  // 标记为"可复用状态"

        pool.push(user);   // 放回池中
        pool_size++;       // 更新池大小

        logger->debug("User对象归还到连接池, 池大小: {}", pool_size.load());
    }
    else {
        // ⚠️ 池已满 - 丢弃对象（让shared_ptr自动销毁）
        logger->debug("连接池已满，丢弃User对象");
    }
}

size_t ConnectionPool::getPoolSize() {
    return pool_size.load();  // 原子读取
}

void ConnectionPool::cleanup() {
    std::lock_guard<std::mutex> lock(pool_mutex);

    size_t cleaned = pool.size();

    // 清空队列
    while (!pool.empty()) {
        pool.pop();
    }
    pool_size = 0;

    logger->info("🧹 清理连接池，移除 {} 个对象", cleaned);
}

std::size_t ConnectionPool::trimTarget() {
    // 只读一次环境变量（首次调用时）：target >= MAX_POOL_SIZE 表示"不缩"，
    // 这样想保留老的"只涨不缩"行为只要设一个很大的值即可。
    static const std::size_t target = [] {
        const long long raw = envInt("TETRIS_POOL_TRIM_TARGET", 4096);
        if (raw < 0 || raw > static_cast<long long>(MAX_POOL_SIZE)) {
            return MAX_POOL_SIZE;
        }
        return static_cast<std::size_t>(raw);
    }();
    return target;
}

int ConnectionPool::trimIntervalMs() {
    static const int interval = [] {
        const long long raw = envInt("TETRIS_POOL_TRIM_INTERVAL_MS", 5000);
        if (raw < 100 || raw > 3600000) {
            return 5000;
        }
        return static_cast<int>(raw);
    }();
    return interval;
}

std::size_t ConnectionPool::trimIdleObjects(std::size_t target) {
    // 先把要丢掉的对象一次性移出队列（持锁时间很短），**在锁外析构**：
    // 一次可能释放上万个 User（每个还带两个 deque），不该占着 pool_mutex。
    std::deque<std::shared_ptr<User>> dropped;
    std::size_t before = 0;
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        before = pool_size.load();
        if (before <= target) {
            return 0;
        }
        std::size_t toDrop = before - target;
        while (toDrop > 0 && !pool.empty()) {
            dropped.push_back(std::move(pool.front()));
            pool.pop();
            pool_size--;
            --toDrop;
        }
    }

    const std::size_t after = pool_size.load();
    const std::size_t trimmed = before - after;
    dropped.clear();   // 锁外析构
    total_trimmed += static_cast<long>(trimmed);

#if defined(__GLIBC__)
    // 只 free 对象是不够的：glibc 会把空闲块留在 arena 里，RSS 不一定回落。
    // 丢得多时主动把空闲页还给内核（几十 µs~几 ms，且只在 trim 时才走）。
    if (trimmed >= 256) {
        malloc_trim(0);
    }
#endif

    // info 级别：默认日志（warn）下不写盘，bench 的结果目录也不会被这条刷屏
    logger->info("连接池 trim: {} -> {} (target {})", before, after, target);
    return trimmed;
}

void ConnectionPool::maybeTrim() {
    static const std::size_t target = trimTarget();
    static const std::chrono::milliseconds interval{trimIntervalMs()};
    static std::chrono::steady_clock::time_point last{};

    if (target >= MAX_POOL_SIZE) {
        return;   // 关闭收缩
    }

    // 限频：100ms 定时器每 tick 都会调到这里，但真正检查池子最多每 interval 一次
    const auto now = std::chrono::steady_clock::now();
    if (last != std::chrono::steady_clock::time_point{} && now - last < interval) {
        return;
    }
    last = now;

    if (pool_size.load() <= target) {
        return;
    }
    trimIdleObjects(target);
}

void ConnectionPool::printPerformanceStats() {
    logger->info("连接池性能统计 - 峰值使用: {}, 分配失败: {}",
        peak_usage.load(), allocation_failures.load());

    std::cout << "📊 连接池性能统计:" << std::endl;
    std::cout << "  峰值使用: " << peak_usage.load() << std::endl;
    std::cout << "  分配失败: " << allocation_failures.load() << std::endl;
    std::cout << "  总获取: " << total_acquired.load() << std::endl;
    std::cout << "  总释放: " << total_released.load() << std::endl;
    std::cout << "  池外创建: " << create_count.load() << std::endl;
    std::cout << "  累计 trim: " << total_trimmed.load() << std::endl;
}

bool ConnectionPool::isHealthy() {
    return pool_size > (INITIAL_POOL_SIZE * 0.1); // 池中至少还有10%的对象
}