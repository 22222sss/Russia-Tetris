#include "RegThrottle.h"

#include <algorithm>
#include <cstdlib>

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

std::chrono::steady_clock::time_point gLastPrune{};

std::int64_t nowMs(std::chrono::steady_clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

int slotDurationMs(int windowMs) {
    // windowMs 五等分；极端配置下至少 1ms，避免除零
    return std::max(1, windowMs / RegThrottle::kSlots);
}

std::int64_t bucketOf(std::int64_t ms, int slotMs) {
    std::int64_t bucket = ms / slotMs;
    if (ms < 0 && ms % slotMs != 0) {
        --bucket;   // 负数向下取整（steady_clock 一般不会出现，但别留暗坑）
    }
    return bucket;
}

int slotIndex(std::int64_t bucket) {
    const std::int64_t raw = bucket % RegThrottle::kSlots;
    return static_cast<int>(raw < 0 ? raw + RegThrottle::kSlots : raw);
}

template <typename WindowT>
std::uint64_t windowSum(const WindowT& window, std::int64_t curBucket) {
    std::uint64_t total = 0;
    for (int i = 0; i < RegThrottle::kSlots; ++i) {
        const auto& slot = window.slots[i];
        // 只统计"最近 kSlots 个时间片"内的槽；更早的槽已滑出窗口，视为 0
        if (slot.bucket <= curBucket && curBucket - slot.bucket < RegThrottle::kSlots) {
            total += slot.count;
        }
    }
    return total;
}

// 等到什么时候计数才会掉回阈值以下（毫秒时间戳）
template <typename WindowT>
std::int64_t drainAtMs(const WindowT& window, std::int64_t curBucket, int slotMs,
                       std::uint64_t total, int limit) {
    const std::uint64_t threshold = static_cast<std::uint64_t>(std::max(1, limit));

    // 把窗口内的槽按时间片编号升序排好（最多 5 个，插入排序足够）
    std::int64_t bucketIds[RegThrottle::kSlots];
    std::uint32_t counts[RegThrottle::kSlots];
    int n = 0;
    for (int i = 0; i < RegThrottle::kSlots; ++i) {
        const auto& slot = window.slots[i];
        if (slot.count == 0) {
            continue;
        }
        if (slot.bucket > curBucket || curBucket - slot.bucket >= RegThrottle::kSlots) {
            continue;
        }
        int j = n++;
        while (j > 0 && bucketIds[j - 1] > slot.bucket) {
            bucketIds[j] = bucketIds[j - 1];
            counts[j] = counts[j - 1];
            --j;
        }
        bucketIds[j] = slot.bucket;
        counts[j] = slot.count;
    }

    // 从最老的槽开始"减去"：减到总数低于阈值的那一刻，就是最早可能放行的时间。
    // 一个槽在 (bucket + kSlots) * slotMs 时滑出窗口。
    std::uint64_t acc = total;
    for (int i = 0; i < n; ++i) {
        acc -= counts[i];
        if (acc < threshold) {
            return (bucketIds[i] + RegThrottle::kSlots) * static_cast<std::int64_t>(slotMs);
        }
    }

    // 兜底：整窗滑完之后一定是 0
    return (curBucket + 1) * static_cast<std::int64_t>(slotMs);
}

// 一个窗口的判定：窗口内计数 >= limit 就拒绝，并算出还要等多久
template <typename WindowT>
RegThrottle::Decision evaluate(const WindowT& window, int limit, int windowMs,
                               std::int64_t ms) {
    RegThrottle::Decision decision;
    if (limit <= 0) {
        return decision;   // 该维度被关闭
    }

    const int slotMs = slotDurationMs(windowMs);
    const std::int64_t cur = bucketOf(ms, slotMs);
    const std::uint64_t total = windowSum(window, cur);
    if (total < static_cast<std::uint64_t>(limit)) {
        return decision;
    }

    const std::int64_t drainAt = drainAtMs(window, cur, slotMs, total, limit);
    const std::int64_t remainMs = drainAt - ms;
    decision.throttled = true;
    decision.retryAfterSeconds = static_cast<int>((remainMs + 999) / 1000);
    if (decision.retryAfterSeconds < 1) {
        decision.retryAfterSeconds = 1;
    }
    return decision;
}

template <typename WindowT>
void bump(WindowT& window, int windowMs, std::int64_t ms) {
    const int slotMs = slotDurationMs(windowMs);
    const std::int64_t cur = bucketOf(ms, slotMs);
    auto& slot = window.slots[slotIndex(cur)];
    if (slot.bucket != cur) {
        slot.bucket = cur;
        slot.count = 0;
    }
    if (slot.count < UINT32_MAX) {
        ++slot.count;
    }
}

}  // namespace

RegThrottle::Config RegThrottle::configFromEnv() {
    Config cfg;

    const long long ipMax = envInt("TETRIS_REG_IP_MAX", cfg.ipMax);
    if (ipMax <= 1000000) {
        cfg.ipMax = static_cast<int>(ipMax);   // 0 = 关闭"按 IP"这一维
    }

    // 窗口必须明显长于"一次注册的最坏耗时"，否则正常用户重试会被反复拦住
    const long long ipWindowMs = envInt("TETRIS_REG_IP_WINDOW_MS", cfg.ipWindowMs);
    if (ipWindowMs >= 1000 && ipWindowMs <= 24LL * 3600 * 1000) {
        cfg.ipWindowMs = static_cast<int>(ipWindowMs);
    }

    const long long globalMax = envInt("TETRIS_REG_GLOBAL_MAX", cfg.globalMax);
    if (globalMax <= 1000000) {
        cfg.globalMax = static_cast<int>(globalMax);   // 0 = 关闭"全局"这一维
    }

    const long long globalWindowMs = envInt("TETRIS_REG_GLOBAL_WINDOW_MS", cfg.globalWindowMs);
    if (globalWindowMs >= 1000 && globalWindowMs <= 24LL * 3600 * 1000) {
        cfg.globalWindowMs = static_cast<int>(globalWindowMs);
    }

    const long long maxEntries = envInt("TETRIS_REG_MAX_ENTRIES",
                                        static_cast<long long>(cfg.maxEntries));
    if (maxEntries >= 1 && maxEntries <= 1000000) {
        cfg.maxEntries = static_cast<std::size_t>(maxEntries);
    }

    return cfg;
}

RegThrottle::Config& RegThrottle::configRef() {
    static Config instance = configFromEnv();
    return instance;
}

RegThrottle::Config RegThrottle::config() {
    return configRef();
}

RegThrottle::Window& RegThrottle::globalWindow() {
    static Window instance;
    return instance;
}

std::unordered_map<std::uint32_t, RegThrottle::Window>& RegThrottle::entries() {
    static std::unordered_map<std::uint32_t, Window> instance;
    return instance;
}

RegThrottle::Decision RegThrottle::check(std::uint32_t ip,
                                         std::chrono::steady_clock::time_point now) {
    const Config& cfg = configRef();
    const std::int64_t ms = nowMs(now);

    // 先判全局：它挡的是"换一批 IP 分散喷洒"，比单 IP 额度更硬
    if (cfg.globalMax > 0) {
        Decision decision = evaluate(globalWindow(), cfg.globalMax, cfg.globalWindowMs, ms);
        if (decision.throttled) {
            decision.global = true;
            return decision;
        }
    }

    if (ip == 0 || cfg.ipMax <= 0) {
        return Decision{};   // 未知来源不参与"按 IP"这一维
    }

    auto& map = entries();
    auto it = map.find(ip);
    if (it == map.end()) {
        return Decision{};
    }

    const int slotMs = slotDurationMs(cfg.ipWindowMs);
    const std::int64_t cur = bucketOf(ms, slotMs);
    if (windowSum(it->second, cur) == 0) {
        // 整窗都过期了：等价于没注册过，顺手清掉
        map.erase(it);
        return Decision{};
    }
    return evaluate(it->second, cfg.ipMax, cfg.ipWindowMs, ms);
}

void RegThrottle::onAttempt(std::uint32_t ip,
                            std::chrono::steady_clock::time_point now) {
    const Config& cfg = configRef();
    const std::int64_t ms = nowMs(now);

    if (cfg.globalMax > 0) {
        bump(globalWindow(), cfg.globalWindowMs, ms);
    }

    if (ip == 0 || cfg.ipMax <= 0) {
        return;
    }

    // 按时间节流地清理过期条目（最多每秒一次）；条目满了对新 IP fail-open，
    // 已有记录仍然生效（与 IpThrottle 同一策略）。
    auto& map = entries();
    const auto sinceLastPrune = ms - nowMs(gLastPrune);
    if (sinceLastPrune >= 1000 || map.size() >= cfg.maxEntries) {
        gLastPrune = now;
        const int slotMs = slotDurationMs(cfg.ipWindowMs);
        const std::int64_t cur = bucketOf(ms, slotMs);
        for (auto it = map.begin(); it != map.end();) {
            if (windowSum(it->second, cur) == 0) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto it = map.find(ip);
    if (it == map.end()) {
        if (map.size() >= cfg.maxEntries) {
            return;
        }
        it = map.emplace(ip, Window{}).first;
    }
    bump(it->second, cfg.ipWindowMs, ms);
}

void RegThrottle::setConfigForTest(const Config& config) {
    configRef() = config;
    resetForTest();
}

void RegThrottle::resetForTest() {
    entries().clear();
    for (int i = 0; i < kSlots; ++i) {
        globalWindow().slots[i] = Slot{};
    }
    gLastPrune = std::chrono::steady_clock::time_point{};
}
