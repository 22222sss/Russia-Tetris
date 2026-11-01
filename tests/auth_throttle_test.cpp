// AuthThrottle 自检：在 CI 里单独编译运行（见 .github/workflows/build.yml）。
//
// 为什么单独测：退避逻辑是纯状态机，直接注入时间就能把"指数增长 / 封顶 / 成功清零 /
// 过期 / 窗口内尝试不延长 / 条目上限"这些边界全部枚举出来，不需要起服务端、也不依赖真实时钟。
//
// 编译运行（在 src/ 下）：
//   g++ -std=c++17 -Wall -Wextra -I. TetrisGame/AuthThrottle.cpp \
//       ../tests/auth_throttle_test.cpp -pthread -o /tmp/at_test && /tmp/at_test

#include "TetrisGame/AuthThrottle.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

int gFailures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++gFailures;
    }
}

AuthThrottle::Config testConfig() {
    AuthThrottle::Config cfg;
    cfg.freeAttempts = 5;
    cfg.baseDelayMs = 1000;
    cfg.maxDelayMs = 30000;
    cfg.entryTtlMs = 15 * 60 * 1000;
    cfg.maxEntries = 100;
    return cfg;
}

// 用固定的时间原点 + 偏移构造时间点，避免依赖真实时钟
Clock::time_point at(int offsetMs) {
    return Clock::time_point{} + std::chrono::hours(1) + milliseconds(offsetMs);
}

}  // namespace

int main() {
    AuthThrottle::setConfigForTest(testConfig());
    const std::string user = "alice";

    // 1) 免延迟次数内正常处理
    bool allFree = true;
    for (int i = 1; i <= testConfig().freeAttempts; ++i) {
        allFree = allFree && !AuthThrottle::check(user, at(0)).throttled;
        AuthThrottle::onFailure(user, at(0));
    }
    check(allFree, "前 5 次失败不被节流");

    // 2) 第 6 次失败后等 1s（base * 2^0）
    AuthThrottle::onFailure(user, at(0));
    AuthThrottle::Decision decision = AuthThrottle::check(user, at(500));
    check(decision.throttled, "第 6 次失败后立即尝试被拒绝");
    check(decision.retryAfterSeconds == 1, "重试等待向上取整到 1 秒（还差 0.5s）");
    check(!AuthThrottle::check(user, at(1000)).throttled, "等满 1 秒后放行");

    // 3) 继续失败按 2 倍增长，直到 30s 封顶（k=7..11）
    int now = 1000;
    const int expectedDelayMs[] = {2000, 4000, 8000, 16000, 30000};
    bool growthOk = true;
    bool capOk = true;
    for (int i = 0; i < 5; ++i) {
        AuthThrottle::onFailure(user, at(now));
        const auto active = AuthThrottle::check(user, at(now + 1));
        const int expectedSeconds = (expectedDelayMs[i] + 999) / 1000;
        growthOk = growthOk && active.throttled && active.retryAfterSeconds == expectedSeconds;
        if (expectedDelayMs[i] == 30000) {
            capOk = capOk && active.retryAfterSeconds == 30;
        }
        now += expectedDelayMs[i];
    }
    check(growthOk, "失败后延迟按 2 倍增长（2s/4s/8s/16s/30s）");
    check(capOk, "延迟封顶在 30 秒");

    // 4) 窗口内的尝试只被拒绝，不延长窗口
    // 上面的循环正好走到"封顶等待结束"的时刻，先再失败一次制造一个新的 30s 窗口
    AuthThrottle::onFailure(user, at(now));
    const auto first = AuthThrottle::check(user, at(now + 1000));
    const auto second = AuthThrottle::check(user, at(now + 1000));
    check(first.throttled && second.retryAfterSeconds == first.retryAfterSeconds,
          "窗口内重复尝试不会延长等待时间");
    check(!AuthThrottle::check(user, at(now + 30000)).throttled, "封顶等待结束后放行");

    // 5) 成功登录立即清零
    AuthThrottle::onSuccess(user);
    check(!AuthThrottle::check(user, at(now + 30000)).throttled, "登录成功后不再节流");
    AuthThrottle::onFailure(user, at(now + 30000));
    check(!AuthThrottle::check(user, at(now + 30000)).throttled, "清零后重新从免延迟次数开始");

    // 6) 15 分钟没有新失败则过期
    const int expiredAt = now + 30000 + testConfig().entryTtlMs + 1;
    check(!AuthThrottle::check(user, at(expiredAt)).throttled, "过期记录不再节流");
    AuthThrottle::onFailure(user, at(expiredAt));
    check(!AuthThrottle::check(user, at(expiredAt)).throttled, "过期后重新计数（第 1 次仍免延迟）");

    // 7) 未知用户名按同样的规则计数（避免靠"是否被节流"判断账号是否存在）
    const std::string missing = "no-such-user";
    for (int i = 0; i < testConfig().freeAttempts + 1; ++i) {
        AuthThrottle::onFailure(missing, at(0));
    }
    check(AuthThrottle::check(missing, at(1)).throttled, "不存在的用户名同样被节流");

    // 8) 条目上限：满了之后不再跟踪新用户名（对随机喷洒 fail-open），已有记录不受影响
    AuthThrottle::Config small = testConfig();
    small.maxEntries = 2;
    AuthThrottle::setConfigForTest(small);
    for (int i = 0; i < small.freeAttempts + 1; ++i) {
        AuthThrottle::onFailure("u1", at(0));
        AuthThrottle::onFailure("u2", at(0));
        AuthThrottle::onFailure("u3", at(0));
    }
    check(AuthThrottle::check("u3", at(1)).throttled == false, "条目满后不再跟踪新用户名");
    check(AuthThrottle::check("u1", at(1)).throttled, "已有记录仍然有效");
    AuthThrottle::onSuccess("u1");
    for (int i = 0; i < small.freeAttempts + 1; ++i) {
        AuthThrottle::onFailure("u3", at(0));
    }
    check(AuthThrottle::check("u3", at(1)).throttled, "腾出名额后新用户名恢复跟踪");

    // 9) 超长用户名按前 64 字符归并（防止超长 key 放大内存）
    AuthThrottle::setConfigForTest(testConfig());
    const std::string prefix(64, 'a');
    for (int i = 0; i < testConfig().freeAttempts + 1; ++i) {
        AuthThrottle::onFailure(prefix + "X", at(0));
    }
    check(AuthThrottle::check(prefix + "Y", at(1)).throttled,
          "超长用户名按前 64 字符跟踪");

    if (gFailures != 0) {
        std::printf("%d 项自检失败\n", gFailures);
        return 1;
    }
    std::printf("登录退避自检全部通过\n");
    return 0;
}
