#include "IpThrottle.h"

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

}  // namespace

IpThrottle::Config IpThrottle::configFromEnv() {
    Config cfg;

    const long long maxFailures = envInt("TETRIS_AUTH_IP_MAX_FAILURES", cfg.maxFailures);
    if (maxFailures >= 1 && maxFailures <= 1000000) {
        cfg.maxFailures = static_cast<int>(maxFailures);
    }

    // 窗口必须明显长于"一次登录的最坏耗时"，否则正常用户重试会被反复拦住。
    const long long windowMs = envInt("TETRIS_AUTH_IP_WINDOW_MS", cfg.windowMs);
    if (windowMs >= 1000 && windowMs <= 24LL * 3600 * 1000) {
        cfg.windowMs = static_cast<int>(windowMs);
    }

    const long long maxEntries = envInt("TETRIS_AUTH_IP_MAX_ENTRIES",
                                        static_cast<long long>(cfg.maxEntries));
    if (maxEntries >= 1 && maxEntries <= 1000000) {
        cfg.maxEntries = static_cast<std::size_t>(maxEntries);
    }

    return cfg;
}

IpThrottle::Config& IpThrottle::config() {
    static Config instance = configFromEnv();
    return instance;
}

std::unordered_map<std::uint32_t, IpThrottle::Entry>& IpThrottle::entries() {
    static std::unordered_map<std::uint32_t, Entry> instance;
    return instance;
}

std::int64_t IpThrottle::nowMs(std::chrono::steady_clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

int IpThrottle::slotDurationMs() {
    // windowMs 五等分；极端配置下至少 1ms，避免除零
    return std::max(1, config().windowMs / kSlots);
}

std::int64_t IpThrottle::bucketOf(std::int64_t ms, int slotMs) {
    std::int64_t bucket = ms / slotMs;
    if (ms < 0 && ms % slotMs != 0) {
        --bucket;   // 负数向下取整（steady_clock 一般不会出现，但别留暗坑）
    }
    return bucket;
}

int IpThrottle::slotIndex(std::int64_t bucket) {
    const std::int64_t raw = bucket % kSlots;
    return static_cast<int>(raw < 0 ? raw + kSlots : raw);
}

std::uint64_t IpThrottle::windowSum(const Entry& entry, std::int64_t curBucket) {
    std::uint64_t total = 0;
    for (int i = 0; i < kSlots; ++i) {
        const Slot& slot = entry.slots[i];
        // 只统计"最近 kSlots 个时间片"内的槽；更早的槽已滑出窗口，视为 0
        if (slot.bucket <= curBucket && curBucket - slot.bucket < kSlots) {
            total += slot.count;
        }
    }
    return total;
}

std::int64_t IpThrottle::drainAtMs(const Entry& entry, std::int64_t curBucket,
                                   int slotMs, std::uint64_t total) {
    const std::uint64_t limit = static_cast<std::uint64_t>(std::max(1, config().maxFailures));

    // 把窗口内的槽按时间片编号升序排好（最多 5 个，插入排序足够）
    std::int64_t bucketIds[kSlots];
    std::uint32_t counts[kSlots];
    int n = 0;
    for (int i = 0; i < kSlots; ++i) {
        const Slot& slot = entry.slots[i];
        if (slot.count == 0) {
            continue;
        }
        if (slot.bucket > curBucket || curBucket - slot.bucket >= kSlots) {
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
        if (acc < limit) {
            return (bucketIds[i] + kSlots) * static_cast<std::int64_t>(slotMs);
        }
    }

    // 兜底：整窗滑完之后一定是 0
    return (curBucket + 1) * static_cast<std::int64_t>(slotMs);
}

void IpThrottle::pruneIfNeeded(std::chrono::steady_clock::time_point now) {
    auto& map = entries();
    const auto ms = nowMs(now);
    const auto sinceLastPrune = ms - nowMs(gLastPrune);
    if (sinceLastPrune < 1000 && map.size() < config().maxEntries) {
        return;
    }

    gLastPrune = now;
    const int slotMs = slotDurationMs();
    const std::int64_t cur = bucketOf(ms, slotMs);

    for (auto it = map.begin(); it != map.end();) {
        if (windowSum(it->second, cur) == 0) {
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

IpThrottle::Decision IpThrottle::check(std::uint32_t ip,
                                       std::chrono::steady_clock::time_point now) {
    Decision decision;
    if (ip == 0) {
        return decision;   // 未知来源（理论上不会出现）：不参与限流
    }

    auto& map = entries();
    auto it = map.find(ip);
    if (it == map.end()) {
        return decision;
    }

    const int slotMs = slotDurationMs();
    const std::int64_t ms = nowMs(now);
    const std::int64_t cur = bucketOf(ms, slotMs);

    const std::uint64_t total = windowSum(it->second, cur);
    if (total == 0) {
        // 整窗都过期了：等价于没有失败过，顺手清掉
        map.erase(it);
        return decision;
    }

    const std::uint64_t limit = static_cast<std::uint64_t>(std::max(1, config().maxFailures));
    if (total < limit) {
        return decision;
    }

    const std::int64_t drainAt = drainAtMs(it->second, cur, slotMs, total);
    const std::int64_t remainMs = drainAt - ms;
    decision.throttled = true;
    decision.retryAfterSeconds = static_cast<int>((remainMs + 999) / 1000);
    if (decision.retryAfterSeconds < 1) {
        decision.retryAfterSeconds = 1;
    }
    return decision;
}

void IpThrottle::onFailure(std::uint32_t ip,
                           std::chrono::steady_clock::time_point now) {
    if (ip == 0) {
        return;
    }

    pruneIfNeeded(now);

    auto& map = entries();
    auto it = map.find(ip);
    if (it == map.end()) {
        // 条目满了就不再跟踪新 IP（对"很多源 IP 的喷洒" fail-open）：
        // 已有记录仍然有效；与 AuthThrottle 一样，宁可漏记也不挤掉正在生效的记录。
        if (map.size() >= config().maxEntries) {
            return;
        }
        it = map.emplace(ip, Entry{}).first;
    }

    const int slotMs = slotDurationMs();
    const std::int64_t cur = bucketOf(nowMs(now), slotMs);
    Slot& slot = it->second.slots[slotIndex(cur)];
    if (slot.bucket != cur) {
        slot.bucket = cur;
        slot.count = 0;
    }
    if (slot.count < UINT32_MAX) {
        ++slot.count;
    }
}

std::string IpThrottle::format(std::uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFFu) + "." +
           std::to_string((ip >> 16) & 0xFFu) + "." +
           std::to_string((ip >> 8) & 0xFFu) + "." +
           std::to_string(ip & 0xFFu);
}

void IpThrottle::setConfigForTest(const Config& config) {
    IpThrottle::config() = config;
    resetForTest();
}

void IpThrottle::resetForTest() {
    entries().clear();
    gLastPrune = std::chrono::steady_clock::time_point{};
}
