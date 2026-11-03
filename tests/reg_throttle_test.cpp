// RegThrottle 自检：在 CI 里单独编译运行（见 .github/workflows/build.yml）。
//
// 为什么单独测：注册限流是纯状态机（时间由参数注入），"每 IP / 全局两个窗口、
// 拒绝不记账、窗口滑动、未知来源只算全局、条目上限 fail-open、0 = 关闭维度"
// 这些边界可以全部枚举，不用起服务端、也不依赖真实时钟。
//
// 编译运行（在 src/ 下）：
//   g++ -std=c++17 -Wall -Wextra -I. TetrisGame/RegThrottle.cpp \
//       ../tests/reg_throttle_test.cpp -o /tmp/rt_test && /tmp/rt_test

#include "TetrisGame/RegThrottle.h"

#include <chrono>
#include <cstdint>
#include <cstdio>

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

// 窗口 5s，5 个槽 → 每槽 1s，便于手算边界。
// 默认只考"按 IP"这一维（全局额度放得很宽），需要时再单独收紧。
RegThrottle::Config ipConfig() {
    RegThrottle::Config cfg;
    cfg.ipMax = 3;
    cfg.ipWindowMs = 5000;
    cfg.globalMax = 1000;
    cfg.globalWindowMs = 5000;
    cfg.maxEntries = 100;
    return cfg;
}

RegThrottle::Config globalConfig() {
    RegThrottle::Config cfg = ipConfig();
    cfg.ipMax = 1000;      // 关掉"按 IP"的干扰
    cfg.globalMax = 3;
    return cfg;
}

// 固定时间原点 + 偏移，避免依赖真实时钟；偏移是 1000ms 的整数倍才对得上槽位
Clock::time_point at(int offsetMs) {
    return Clock::time_point{} + std::chrono::hours(1) + milliseconds(offsetMs);
}

// 模拟"一次注册"：先 check（放行才提交哈希），再记账
bool attempt(std::uint32_t ip, int offsetMs) {
    if (RegThrottle::check(ip, at(offsetMs)).throttled) {
        return false;
    }
    RegThrottle::onAttempt(ip, at(offsetMs));
    return true;
}

}  // namespace

int main() {
    const std::uint32_t ipA = 0x0A000001;   // 10.0.0.1
    const std::uint32_t ipB = 0x0A000002;   // 10.0.0.2
    const std::uint32_t ipC = 0x0A000003;   // 10.0.0.3

    // 1) 阈值内放行，达到阈值后拦截；拦截标记是"单 IP"而不是"全局"
    RegThrottle::setConfigForTest(ipConfig());
    bool allowed = true;
    for (int i = 0; i < ipConfig().ipMax; ++i) {
        allowed = allowed && attempt(ipA, 0);
    }
    check(allowed, "没到阈值前不拦截");
    const auto blocked = RegThrottle::check(ipA, at(0));
    check(blocked.throttled, "达到阈值后拦截（不跑 PBKDF2）");
    check(!blocked.global, "按 IP 拦截时 global 标记为 false");

    // 2) 等待时间：整窗滑完才放行，向上取整到秒
    check(blocked.retryAfterSeconds == 5, "等待时间向上取整（整窗 5s）");
    check(RegThrottle::check(ipA, at(4000)).throttled, "窗口内仍然拦截");
    check(RegThrottle::check(ipA, at(4000)).retryAfterSeconds == 1, "只剩 1s 时提示 1 秒");
    check(!RegThrottle::check(ipA, at(5000)).throttled, "整窗滑过后放行");

    // 3) 被拒绝的尝试不记账、不延长窗口
    RegThrottle::setConfigForTest(ipConfig());
    for (int i = 0; i < ipConfig().ipMax; ++i) {
        RegThrottle::onAttempt(ipA, at(0));
    }
    const auto first = RegThrottle::check(ipA, at(1000));
    const auto second = RegThrottle::check(ipA, at(3000));
    check(first.throttled && second.throttled, "窗口内多次尝试都被拒绝");
    check(first.retryAfterSeconds == 4 && second.retryAfterSeconds == 2,
          "拒绝期间反复尝试不会推后放行时间");
    check(!RegThrottle::check(ipA, at(5000)).throttled, "窗口自然滑过后自动放行");

    // 4) 跨时间片累计：滑动窗口不是"每分钟重新计数"
    RegThrottle::setConfigForTest(ipConfig());
    attempt(ipA, 0);
    attempt(ipA, 0);
    attempt(ipA, 4000);    // 落到第 5 个槽的末尾
    const auto crossSlot = RegThrottle::check(ipA, at(4000));
    check(crossSlot.throttled, "跨时间片的注册会被一起累计");
    check(crossSlot.retryAfterSeconds == 1, "只等最老的那次滑出窗口（1s）");
    check(!RegThrottle::check(ipA, at(5000)).throttled, "最老的滑出后立刻放行");

    // 5) 不同 IP 互不影响（NAT 后面各玩各的）
    RegThrottle::setConfigForTest(ipConfig());
    for (int i = 0; i < ipConfig().ipMax; ++i) {
        RegThrottle::onAttempt(ipA, at(0));
    }
    check(RegThrottle::check(ipA, at(0)).throttled, "A 被拦");
    check(!RegThrottle::check(ipB, at(0)).throttled, "B 不受 A 影响");
    check(attempt(ipB, 0), "B 仍能正常注册");

    // 6) 未知来源（ip == 0）不参与"按 IP"，但仍然计入全局
    RegThrottle::setConfigForTest(globalConfig());
    bool unknownAllowed = true;
    for (int i = 0; i < globalConfig().globalMax; ++i) {
        unknownAllowed = unknownAllowed && attempt(0, 0);
    }
    check(unknownAllowed, "来源未知时按 IP 这一维不限流");
    const auto unknownBlocked = RegThrottle::check(0, at(0));
    check(unknownBlocked.throttled && unknownBlocked.global,
          "来源未知时全局额度仍然生效");

    // 7) 全局额度：不同 IP 各在自己的额度内，总量到顶后照样拦
    RegThrottle::setConfigForTest(globalConfig());
    check(attempt(ipA, 0) && attempt(ipB, 0) && attempt(ipC, 0), "三个 IP 各注册一次");
    const auto globalBlocked = RegThrottle::check(0x0A000004, at(0));
    check(globalBlocked.throttled && globalBlocked.global,
          "总量到顶后新 IP 也被拦（换 IP 喷洒挡得住）");
    check(globalBlocked.retryAfterSeconds <= 5, "全局等待时间不超过整窗");

    // 8) 全局窗口也会自然滑动
    check(!RegThrottle::check(0x0A000004, at(5000)).throttled, "全局窗口滑过后放行");

    // 9) 条目上限：满了之后不再跟踪新 IP（fail-open），已有记录仍然生效
    RegThrottle::Config small = ipConfig();
    small.maxEntries = 2;
    small.ipMax = 2;
    RegThrottle::setConfigForTest(small);
    for (int i = 0; i < small.ipMax; ++i) {
        RegThrottle::onAttempt(ipA, at(0));
        RegThrottle::onAttempt(ipB, at(0));
        RegThrottle::onAttempt(ipC, at(0));   // 第三个 IP：条目已满，不跟踪
    }
    check(RegThrottle::check(ipA, at(0)).throttled, "已有记录仍然生效");
    check(!RegThrottle::check(ipC, at(0)).throttled, "条目满后不再跟踪新 IP");

    // 10) 阈值 0 = 关闭该维度（公测当天可能只留一道）
    RegThrottle::Config ipOnly = ipConfig();
    ipOnly.globalMax = 0;
    RegThrottle::setConfigForTest(ipOnly);
    check(attempt(ipA, 0) && attempt(ipA, 0) && attempt(ipA, 0),
          "全局额度设 0 不影响按 IP 放行");
    const auto ipOnlyBlocked = RegThrottle::check(ipA, at(0));
    check(ipOnlyBlocked.throttled && !ipOnlyBlocked.global,
          "全局额度设 0 时仍然按 IP 拦截");

    RegThrottle::Config globalOnly = globalConfig();
    globalOnly.ipMax = 0;
    RegThrottle::setConfigForTest(globalOnly);
    bool perIpAlwaysAllowed = true;
    for (int i = 0; i < globalOnly.globalMax; ++i) {
        perIpAlwaysAllowed = perIpAlwaysAllowed && attempt(ipA, 0);
    }
    check(perIpAlwaysAllowed, "单 IP 额度设 0 时同一 IP 也不会被按 IP 拦");
    check(RegThrottle::check(ipA, at(0)).global, "此时拦住它的是全局额度");

    // 11) 配置从环境变量读（非法值保留默认；0 合法 = 关闭）
    RegThrottle::setConfigForTest(RegThrottle::Config{});
    check(RegThrottle::config().ipMax == 20 && RegThrottle::config().globalMax == 200,
          "默认配置是 20 次/5 分钟 + 200 次/小时");

    if (gFailures != 0) {
        std::printf("%d 项自检失败\n", gFailures);
        return 1;
    }
    std::printf("注册限流自检全部通过\n");
    return 0;
}
