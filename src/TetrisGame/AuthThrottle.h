#pragma once
#ifndef AuthThrottle_H
#define AuthThrottle_H

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>

// 登录失败退避：**按用户名指数延迟**，不锁号。
//
// 规则（默认值，可用环境变量覆盖）：
//   * 前 freeAttempts 次失败不加延迟（打错字容错）；
//   * 第 k 次失败（k > freeAttempts）后，下一次尝试要等
//     min(baseDelayMs * 2^(k-freeAttempts-1), maxDelayMs) 毫秒；
//   * 成功登录立即清零；
//   * 等待窗口内的尝试只会被拒绝，**不延长窗口**——否则攻击者可以靠反复
//     触发把别人的账号永久锁死（锁号 DoS 比爆破更糟）；
//   * entryTtlMs 内没有新的失败，整条记录过期（等价于清零），避免长期积累。
//
// 为什么按用户名而不是只按 IP：同一个 NAT 后面可能有多个玩家；而按用户名
// 才真正限制"针对某个账号"的爆破速度。IP 维度的兜底留给后续（见 HANDOVER）。
//
// 线程模型：只在事件循环线程上调用（密码提交与登录回调都在该线程），
// 所以内部不加锁。单元测试通过参数注入时间，不依赖真实时钟。
class AuthThrottle {
public:
    struct Config {
        int freeAttempts = 5;                 // 免延迟失败次数
        int baseDelayMs = 1000;               // 第 free+1 次失败后的基础延迟
        int maxDelayMs = 30000;               // 单次延迟上限
        int entryTtlMs = 15 * 60 * 1000;      // 记录过期时间
        std::size_t maxEntries = 20000;       // 最多跟踪多少个用户名（防内存放大）
    };

    struct Decision {
        bool throttled = false;
        // throttled 时还要等多少秒（向上取整，至少 1）
        int retryAfterSeconds = 0;
    };

    // 不做任何状态修改；过期条目会被顺手清掉
    static Decision check(const std::string& username,
                          std::chrono::steady_clock::time_point now);

    // 记录一次失败：failures++ 并计算 nextAllowed
    static void onFailure(const std::string& username,
                          std::chrono::steady_clock::time_point now);

    // 登录成功：清掉该用户名的记录
    static void onSuccess(const std::string& username);

    // 测试用：覆盖配置并清空状态（生产路径不会调用）
    static void setConfigForTest(const Config& config);
    static void resetForTest();

    // 从环境变量读取配置（首次访问时执行；非法值保留默认）：
    //   TETRIS_AUTH_FREE_ATTEMPTS  TETRIS_AUTH_BASE_DELAY_MS
    //   TETRIS_AUTH_MAX_DELAY_MS   TETRIS_AUTH_ENTRY_TTL_MS
    //   TETRIS_AUTH_MAX_ENTRIES
    static Config configFromEnv();

private:
    struct Entry {
        int failures = 0;
        std::chrono::steady_clock::time_point lastFailure{};
        std::chrono::steady_clock::time_point nextAllowed{};
    };

    static Config& config();
    static std::unordered_map<std::string, Entry>& entries();
    static std::chrono::milliseconds delayFor(int failures);
    // 截断超长用户名，避免攻击者用超长 key 放大内存
    static std::string normalize(const std::string& username);
    // 按时间节流地清理过期条目（最多每秒一次）
    static void pruneIfNeeded(std::chrono::steady_clock::time_point now);
};

#endif
