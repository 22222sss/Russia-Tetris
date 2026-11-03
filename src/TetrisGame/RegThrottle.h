#pragma once
#ifndef RegThrottle_H
#define RegThrottle_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

// 注册限流：按来源 IP + 全局，两道滑动窗口。
//
// 为什么需要它（HANDOVER 6.30）：登录有三层限流，**注册一层都没有**。注册的 PBKDF2
// 跑在唯一的异步写线程上，和"登录凭据查询 / 成绩写入 / 排行榜读取"排在同一条队列里；
// 而注册又是开放的（不需要旧密码），一个客户端每秒能塞进 ~20 个注册，把写线程占满后
// 正常登录的延迟会无界增长。这是上线前唯一"免费"的 PBKDF2 入口。
//
// 规则（都可用环境变量覆盖）：
//   * 按来源 IP：ipWindowMs（默认 5 分钟）内最多 ipMax（默认 20）次注册；
//   * 全局：globalWindowMs（默认 1 小时）内最多 globalMax（默认 200）次注册 ——
//     挡"换一批 IP 分散喷洒"：每个 IP 都在自己额度内，但总量仍有上限；
//   * 两个维度都是 5 个等长槽位的滑动窗口（同 IpThrottle），不会像固定窗口那样在
//     边界上放出两倍额度；
//   * **被拒绝的尝试不记账**：拒绝本身不花 PBKDF2，记账只会让"等一会儿再来"的人
//     被越锁越久（与 AuthThrottle/IpThrottle 同一条原则）；
//   * 记账只发生在"**真的要算哈希**之前"（Server::receive_password_register），
//     所以密码不满足策略、用户名重复这类"没进哈希"的失败不占额度；
//   * 阈值设 0 = 关闭该维度（例如公测当天想让全局不限，但保留单 IP 兜底）。
//
// 为什么不复用 IpThrottle：它的配置是**类级静态**的（一份 config 服务所有维度），
// 而这里需要"每 IP 5 分钟 20 次"和"全局 1 小时 200 次"两套窗口参数。
//
// 线程模型：只在事件循环线程上调用，内部不加锁。单元测试注入时间，不依赖真实时钟。
class RegThrottle {
public:
    struct Config {
        int ipMax = 20;                        // 单个 IP 在窗口内允许的注册次数
        int ipWindowMs = 5 * 60 * 1000;        // 单 IP 窗口长度
        int globalMax = 200;                   // 全进程在窗口内允许的注册次数
        int globalWindowMs = 60 * 60 * 1000;   // 全局窗口长度
        std::size_t maxEntries = 4096;         // 最多跟踪多少个 IP（防内存放大）
    };

    struct Decision {
        bool throttled = false;
        // throttled 时还要等多少秒（向上取整，至少 1）：等到窗口内的注册数
        // 掉回阈值以下为止
        int retryAfterSeconds = 0;
        // true = 被"全局额度"拦住（日志里区分一下，排查时有用）
        bool global = false;
    };

    static constexpr int kSlots = 5;

    // 只读：不改计数（只会顺手清掉完全过期的条目）
    static Decision check(std::uint32_t ip, std::chrono::steady_clock::time_point now);

    // 记录一次"确定要跑哈希"的注册。必须在提交异步哈希之前调用。
    static void onAttempt(std::uint32_t ip, std::chrono::steady_clock::time_point now);

    // 从环境变量读取配置（首次访问时执行；非法值保留默认）：
    //   TETRIS_REG_IP_MAX / _WINDOW_MS / TETRIS_REG_GLOBAL_MAX / _WINDOW_MS
    //   TETRIS_REG_MAX_ENTRIES
    static Config configFromEnv();
    static Config config();   // 生效值（main.cpp 打印用）

    // 测试用：覆盖配置并清空状态（生产路径不会调用）
    static void setConfigForTest(const Config& config);
    static void resetForTest();

private:
    struct Slot {
        std::int64_t bucket = 0;   // 该槽当前属于哪个时间片（windowMs/kSlots）
        std::uint32_t count = 0;   // 该时间片内的注册次数
    };
    struct Window {
        Slot slots[kSlots];
    };

    static Window& globalWindow();
    static std::unordered_map<std::uint32_t, Window>& entries();
    // 可写的静态配置（config() 返回拷贝，内部和测试都走这个）
    static Config& configRef();
};

#endif
