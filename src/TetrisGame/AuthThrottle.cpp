#include "AuthThrottle.h"

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

AuthThrottle::Config AuthThrottle::configFromEnv() {
    Config cfg;

    const long long freeAttempts = envInt("TETRIS_AUTH_FREE_ATTEMPTS", cfg.freeAttempts);
    if (freeAttempts <= 1000) {
        cfg.freeAttempts = static_cast<int>(freeAttempts);
    }

    const long long baseDelayMs = envInt("TETRIS_AUTH_BASE_DELAY_MS", cfg.baseDelayMs);
    if (baseDelayMs >= 1 && baseDelayMs <= 3600000) {
        cfg.baseDelayMs = static_cast<int>(baseDelayMs);
    }

    const long long maxDelayMs = envInt("TETRIS_AUTH_MAX_DELAY_MS", cfg.maxDelayMs);
    if (maxDelayMs >= 1 && maxDelayMs <= 24 * 3600 * 1000) {
        cfg.maxDelayMs = static_cast<int>(maxDelayMs);
    }
    if (cfg.maxDelayMs < cfg.baseDelayMs) {
        cfg.maxDelayMs = cfg.baseDelayMs;
    }

    const long long entryTtlMs = envInt("TETRIS_AUTH_ENTRY_TTL_MS", cfg.entryTtlMs);
    if (entryTtlMs >= 1000 && entryTtlMs <= 24 * 3600 * 1000) {
        cfg.entryTtlMs = static_cast<int>(entryTtlMs);
    }

    const long long maxEntries = envInt("TETRIS_AUTH_MAX_ENTRIES", static_cast<long long>(cfg.maxEntries));
    if (maxEntries >= 1 && maxEntries <= 1000000) {
        cfg.maxEntries = static_cast<std::size_t>(maxEntries);
    }

    return cfg;
}

AuthThrottle::Config& AuthThrottle::config() {
    static Config instance = configFromEnv();
    return instance;
}

std::unordered_map<std::string, AuthThrottle::Entry>& AuthThrottle::entries() {
    static std::unordered_map<std::string, Entry> instance;
    return instance;
}

std::string AuthThrottle::normalize(const std::string& username) {
    constexpr std::size_t kMaxKeyLength = 64;
    if (username.size() <= kMaxKeyLength) {
        return username;
    }
    return username.substr(0, kMaxKeyLength);
}

std::chrono::milliseconds AuthThrottle::delayFor(int failures) {
    const Config& cfg = config();
    if (failures <= cfg.freeAttempts) {
        return std::chrono::milliseconds(0);
    }

    int exponent = failures - cfg.freeAttempts - 1;
    if (exponent < 0) {
        exponent = 0;
    }
    if (exponent > 30) {
        exponent = 30;   // 防移位溢出；此时一定已经到 maxDelay
    }

    long long delay = static_cast<long long>(cfg.baseDelayMs) << exponent;
    if (delay > cfg.maxDelayMs || delay < 0) {
        delay = cfg.maxDelayMs;
    }
    return std::chrono::milliseconds(delay);
}

void AuthThrottle::pruneIfNeeded(std::chrono::steady_clock::time_point now) {
    auto& map = entries();
    const auto sinceLastPrune = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - gLastPrune).count();
    if (sinceLastPrune < 1000 && map.size() < config().maxEntries) {
        return;
    }

    gLastPrune = now;
    const auto ttl = std::chrono::milliseconds(config().entryTtlMs);
    for (auto it = map.begin(); it != map.end();) {
        if (now - it->second.lastFailure >= ttl) {
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

AuthThrottle::Decision AuthThrottle::check(const std::string& username,
                                           std::chrono::steady_clock::time_point now) {
    Decision decision;
    const std::string key = normalize(username);
    if (key.empty()) {
        return decision;
    }

    auto& map = entries();
    auto it = map.find(key);
    if (it == map.end()) {
        return decision;
    }

    // 过期记录等价于"没有失败过"：顺手清掉
    if (now - it->second.lastFailure >= std::chrono::milliseconds(config().entryTtlMs)) {
        map.erase(it);
        return decision;
    }

    if (now < it->second.nextAllowed) {
        const auto remainMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            it->second.nextAllowed - now).count();
        decision.throttled = true;
        decision.retryAfterSeconds = static_cast<int>((remainMs + 999) / 1000);
        if (decision.retryAfterSeconds < 1) {
            decision.retryAfterSeconds = 1;
        }
    }
    return decision;
}

void AuthThrottle::onFailure(const std::string& username,
                             std::chrono::steady_clock::time_point now) {
    const std::string key = normalize(username);
    if (key.empty()) {
        return;
    }

    pruneIfNeeded(now);

    auto& map = entries();
    auto it = map.find(key);
    if (it == map.end()) {
        // 到达上限后不再接收新用户名（对"随机用户名喷洒"fail-open）：
        // 已有记录仍然有效，队列上限负责兜住这种喷洒的算力消耗。
        if (map.size() >= config().maxEntries) {
            return;
        }
        it = map.emplace(key, Entry{}).first;
    }

    Entry& entry = it->second;
    if (entry.failures > 0 &&
        now - entry.lastFailure >= std::chrono::milliseconds(config().entryTtlMs)) {
        entry.failures = 0;   // 过期重新计数（pruneIfNeeded 没清到时兜底）
    }

    entry.lastFailure = now;
    ++entry.failures;
    entry.nextAllowed = now + delayFor(entry.failures);
}

void AuthThrottle::onSuccess(const std::string& username) {
    const std::string key = normalize(username);
    if (!key.empty()) {
        entries().erase(key);
    }
}

void AuthThrottle::setConfigForTest(const Config& config) {
    AuthThrottle::config() = config;
    resetForTest();
}

void AuthThrottle::resetForTest() {
    entries().clear();
    gLastPrune = std::chrono::steady_clock::time_point{};
}
