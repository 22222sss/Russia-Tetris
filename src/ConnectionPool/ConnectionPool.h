#pragma once
#ifndef ConnectionPool_H
#define ConnectionPool_H

#include"../Common/Common.h"
#include"../TetrisGame/User.h"

class ConnectionPool {
private:
    // 静态成员 - 全局唯一的连接池
    static std::queue<std::shared_ptr<User>> pool;      // 对象池
    static std::mutex pool_mutex;                       // 保护池的线程安全
    static std::atomic<size_t> pool_size;               // 当前池大小（原子操作）

    // 配置参数 - 增加到支持10万连接
    static const constexpr size_t MAX_POOL_SIZE = 100000;          // 最大池大小
    static const constexpr size_t INITIAL_POOL_SIZE = 50000;       // 初始预创建数量

    //INITIAL_POOL_SIZE 和 MAX_POOL_SIZE 是静态常量成员，需要在源文件中定义
    //将静态常量成员改为 constexpr 或 inline才能直接定义

    // 性能监控
    static std::atomic<long> peak_usage;
    static std::atomic<long> allocation_failures;

    // 禁止实例化 - 纯静态类
    ConnectionPool() = delete;
    ~ConnectionPool() = delete;

public:
    // 初始化连接池 - 预创建对象
    static void initialize();

    // 从池中获取一个User对象
    static std::shared_ptr<User> acquire(int fd);

    // 将User对象归还到池中
    static void release(std::shared_ptr<User> user);

    // 获取当前池大小
    static size_t getPoolSize();

    // 清理池（服务器关闭时调用）
    static void cleanup();

    // 性能监控
    static void printPerformanceStats();
    static bool isHealthy();

    // 统计信息 - 用于监控
    static std::atomic<long> total_acquired;    // 总获取次数
    static std::atomic<long> total_released;    // 总归还次数
    static std::atomic<long> create_count;      // 总创建次数（池外创建）
};

#endif // ConnectionPool_H