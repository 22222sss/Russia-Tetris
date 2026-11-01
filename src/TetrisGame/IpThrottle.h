#pragma once
#ifndef IpThrottle_H
#define IpThrottle_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

// 按来源 IP 的**粗粒度兜底**。
//
// 为什么需要它：AuthThrottle 只按用户名退避，攻击者只要不停换用户名（包括不存在的
// 用户名，每次都要跑一次假 PBKDF2）就能一直占满哈希线程。队列上限只兜住内存和
// 延迟，兜不住吞吐；这里按"来源 IP 在一次滑动窗口内的失败次数"封顶。
//
// 规则（默认值，可用环境变量覆盖）：
//   * 滑动窗口 windowMs（默认 5 分钟）内，某个 IP 的失败次数达到 maxFailures
//     （默认 300）后，该 IP 的登录尝试一律拒绝，**不跑 PBKDF2**；
//   * 窗口是 5 个等长槽位组成的环形数组，所以不会像固定窗口那样在边界处
//     放出"两倍额度"（窗口滑动，不是重新计数）；
//   * 拒绝期间**不记账**：被拒绝的尝试不算失败、也不会把窗口往后推
//     （与 AuthThrottle 同理，避免"靠反复触发把整个 NAT 后面的人锁死"）；
//   * 成功登录**不清零**：注册是开放的，攻击者随时能注册一个账号来"刷成功"
//     重置自己的计数；这里只让窗口自然滑动排出。
//   * 阈值必须放宽（默认 300 次/5 分钟）：一个 NAT/公司出口后面可能有几十个玩家，
//     按用户名的退避才是主防线，IP 这层只用来拦"单源喷洒"。
//
// 线程模型：只在事件循环线程上调用（密码提交与登录回调都在该线程），内部不加锁。
// 单元测试通过参数注入时间，不依赖真实时钟。
class IpThrottle {
public:
    struct Config {
        int maxFailures = 300;             // 滑动窗口内允许的失败次数（达到即拦）
        int windowMs = 5 * 60 * 1000;      // 滑动窗口长度
        std::size_t maxEntries = 4096;     // 最多跟踪多少个 IP（防内存放大）
    };

    struct Decision {
        bool throttled = false;
        // throttled 时还要等多少秒（向上取整，至少 1）：等到窗口内的失败数
        // 掉回阈值以下为止
        int retryAfterSeconds = 0;
    };

    // 5 个槽位：每个槽覆盖 windowMs/5，任一时刻统计的都是最近 windowMs 的失败数
    static constexpr int kSlots = 5;

    // 不做任何状态修改（只会顺手删掉完全过期的条目）
    static Decision check(std::uint32_t ip,
                          std::chrono::steady_clock::time_point now);

    // 记录一次失败（只在"真的校验过且失败"时调用；Busy / 被拒绝的尝试都不算）
    static void onFailure(std::uint32_t ip,
                          std::chrono::steady_clock::time_point now);

    // 日志/注解里用的点分十进制；0 表示"未知来源"，不参与限流
    static std::string format(std::uint32_t ip);

    // 测试用：覆盖配置并清空状态（生产路径不会调用）
    static void setConfigForTest(const Config& config);
    static void resetForTest();

    // 从环境变量读取配置（首次访问时执行；非法值保留默认）：
    //   TETRIS_AUTH_IP_MAX_FAILURES  TETRIS_AUTH_IP_WINDOW_MS
    //   TETRIS_AUTH_IP_MAX_ENTRIES
    static Config configFromEnv();

private:
    struct Slot {
        std::int64_t bucket = 0;   // 该槽当前属于哪个时间片（windowMs/kSlots）
        std::uint32_t count = 0;   // 该时间片内的失败次数
    };
    struct Entry {
        Slot slots[kSlots];
    };

    static Config& config();
    static std::unordered_map<std::uint32_t, Entry>& entries();

    static std::int64_t nowMs(std::chrono::steady_clock::time_point now);
    static int slotDurationMs();
    // 时间片编号（对负时间也向下取整，避免时间原点之前的行为不一致）
    static std::int64_t bucketOf(std::int64_t ms, int slotMs);
    static int slotIndex(std::int64_t bucket);
    // 最近 windowMs 内的失败总数（不属于窗口的槽不计）
    static std::uint64_t windowSum(const Entry& entry, std::int64_t curBucket);
    // 等到什么时候失败数才会掉回阈值以下（毫秒时间戳）
    static std::int64_t drainAtMs(const Entry& entry, std::int64_t curBucket,
                                  int slotMs, std::uint64_t total);
    // 按时间节流地清理过期条目（最多每秒一次）
    static void pruneIfNeeded(std::chrono::steady_clock::time_point now);
};

#endif
