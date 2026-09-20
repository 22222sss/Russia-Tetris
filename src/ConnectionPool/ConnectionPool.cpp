#include "ConnectionPool.h"

extern shared_ptr<spdlog::logger> logger;

// 初始化静态成员
std::queue<std::shared_ptr<User>> ConnectionPool::pool;
std::mutex ConnectionPool::pool_mutex;
std::atomic<size_t> ConnectionPool::pool_size(0);
std::atomic<long> ConnectionPool::total_acquired(0);
std::atomic<long> ConnectionPool::total_released(0);
std::atomic<long> ConnectionPool::create_count(0);
std::atomic<long> ConnectionPool::peak_usage(0);
std::atomic<long> ConnectionPool::allocation_failures(0);

void ConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(pool_mutex);  // 线程安全

    logger->info("🔄 初始化连接池，预创建 {} 个User对象...", INITIAL_POOL_SIZE);

    for (size_t i = 0; i < INITIAL_POOL_SIZE; ++i) {
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

void ConnectionPool::printPerformanceStats() {
    logger->info("连接池性能统计 - 峰值使用: {}, 分配失败: {}",
        peak_usage.load(), allocation_failures.load());

    std::cout << "📊 连接池性能统计:" << std::endl;
    std::cout << "  峰值使用: " << peak_usage.load() << std::endl;
    std::cout << "  分配失败: " << allocation_failures.load() << std::endl;
    std::cout << "  总获取: " << total_acquired.load() << std::endl;
    std::cout << "  总释放: " << total_released.load() << std::endl;
    std::cout << "  池外创建: " << create_count.load() << std::endl;
}

bool ConnectionPool::isHealthy() {
    return pool_size > (INITIAL_POOL_SIZE * 0.1); // 池中至少还有10%的对象
}