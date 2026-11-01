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

    //INITIAL_POOL_SIZE 和 MAX_POOL_SIZE 是静态常量成员，需要在源文件中定义
    //将静态常量成员改为 constexpr 或 inline才能直接定义

    // 性能监控
    static std::atomic<long> peak_usage;
    static std::atomic<long> allocation_failures;

    // 禁止实例化 - 纯静态类
    ConnectionPool() = delete;
    ~ConnectionPool() = delete;

public:
    // 单进程默认预创建数量（多 worker 时由 main 按 worker 数均分后传入 initialize()，
    // 否则 N 个进程会各自预创建这么多，内存直接乘 N）。
    //
    // 这个值原来是为"10 万连接突发"设的 50000：但建连路径实测只占 ~16% 单核，
    // 池空时按需 new 一个 User 只要 2~3 µs，预创建省不下多少 CPU；而每个 User 现在是
    // 1984 B 对象 + 两个空 deque 约 1.15 KB 堆，5 万个就是 ~150 MiB——服务端哪怕只有
    // 200 个玩家，也要常驻这么多内存。改为 2048（约 6 MiB）：能用上池子复用，
    // 又不为"可能到来的十万连接"提前买单（池空时 acquire 会按需创建，峰值不受影响）。
    static const constexpr size_t INITIAL_POOL_SIZE = 2048;

    // 初始化连接池 - 预创建对象
    static void initialize(size_t initial_size = INITIAL_POOL_SIZE);

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