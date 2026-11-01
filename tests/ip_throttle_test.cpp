// IpThrottle 自检：在 CI 里单独编译运行（见 .github/workflows/build.yml）。
//
// 为什么单独测：和 AuthThrottle 一样是纯状态机，时间由参数注入，
// "阈值 / 滑动窗口 / 拒绝不延长窗口 / 条目上限 / 多 IP 隔离"这些边界可以全部枚举，
// 不用起服务端、也不依赖真实时钟。
//
// 编译运行（在 src/ 下）：
//   g++ -std=c++17 -Wall -Wextra -I. TetrisGame/IpThrottle.cpp \
//       ../tests/ip_throttle_test.cpp -pthread -o /tmp/it_test && /tmp/it_test

#include "TetrisGame/IpThrottle.h"

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

// 窗口 5s，5 个槽 → 每槽 1s，便于手算边界
IpThrottle::Config testConfig() {
    IpThrottle::Config cfg;
    cfg.maxFailures = 3;
    cfg.windowMs = 5000;
    cfg.maxEntries = 100;
    return cfg;
}

// 固定时间原点 + 偏移，避免依赖真实时钟；偏移必须是 1000ms 的整数倍才好对槽位
Clock::time_point at(int offsetMs) {
    return Clock::time_point{} + std::chrono::hours(1) + milliseconds(offsetMs);
}

// 模拟"一次登录尝试 + 失败"：先 check（放行才提交），失败后记账
bool attemptFails(std::uint32_t ip, int offsetMs) {
    if (IpThrottle::check(ip, at(offsetMs)).throttled) {
        return false;
    }
    IpThrottle::onFailure(ip, at(offsetMs));
    return true;
}

}  // namespace

int main() {
    const std::uint32_t ipA = 0x0A000001;   // 10.0.0.1
    const std::uint32_t ipB = 0x0A000002;   // 10.0.0.2

    // 1) 阈值内正常放行，达到阈值后拦截
    IpThrottle::setConfigForTest(testConfig());
    bool allowed = true;
    for (int i = 0; i < testConfig().maxFailures; ++i) {
        allowed = allowed && attemptFails(ipA, 0);
    }
    check(allowed, "失败次数没到阈值前不拦截");
    check(IpThrottle::check(ipA, at(0)).throttled,
          "失败次数达到阈值后拦截（不跑 PBKDF2）");

    // 2) 等待时间：整窗滑完才放行，向上取整到秒
    const auto blocked = IpThrottle::check(ipA, at(0));
    check(blocked.retryAfterSeconds == 5, "等待时间向上取整（整窗 5s）");
    check(IpThrottle::check(ipA, at(4000)).throttled, "窗口内仍然拦截");
    check(IpThrottle::check(ipA, at(4000)).retryAfterSeconds == 1, "只剩 1s 时提示 1 秒");
    check(!IpThrottle::check(ipA, at(5000)).throttled, "整窗滑过后放行");

    // 3) 被拒绝的尝试不记账、不延长窗口
    IpThrottle::setConfigForTest(testConfig());
    for (int i = 0; i < testConfig().maxFailures; ++i) {
        IpThrottle::onFailure(ipA, at(0));
    }
    const auto first = IpThrottle::check(ipA, at(1000));
    const auto second = IpThrottle::check(ipA, at(3000));
    check(first.throttled && second.throttled, "窗口内多次尝试都被拒绝");
    check(first.retryAfterSeconds == 4 && second.retryAfterSeconds == 2,
          "拒绝期间反复尝试不会推后放行时间");
    check(!IpThrottle::check(ipA, at(5000)).throttled, "窗口自然滑过后自动放行");

    // 4) 跨时间片累计：滑动窗口不是"每分钟重新计数"
    IpThrottle::setConfigForTest(testConfig());
    attemptFails(ipA, 0);
    attemptFails(ipA, 0);
    attemptFails(ipA, 4000);    // 第 5 个槽的末尾
    const auto crossSlot = IpThrottle::check(ipA, at(4000));
    check(crossSlot.throttled, "跨时间片的失败会被一起累计");
    check(crossSlot.retryAfterSeconds == 1, "只等最老的失败滑出窗口（1s）");
    check(!IpThrottle::check(ipA, at(5000)).throttled, "最老的失败滑出后立刻放行");

    // 5) 不同 IP 互不影响（NAT 之外的单源喷洒才会被拦）
    IpThrottle::setConfigForTest(testConfig());
    for (int i = 0; i < testConfig().maxFailures; ++i) {
        IpThrottle::onFailure(ipA, at(0));
    }
    check(IpThrottle::check(ipA, at(0)).throttled, "A 被拦");
    check(!IpThrottle::check(ipB, at(0)).throttled, "B 不受 A 影响");
    check(attemptFails(ipB, 0), "B 仍能正常尝试");

    // 6) 未知来源（ip == 0）不参与限流：绝不会因为"看起来像同一个 IP"误伤
    IpThrottle::setConfigForTest(testConfig());
    for (int i = 0; i < 10; ++i) {
        IpThrottle::onFailure(0, at(0));
    }
    check(!IpThrottle::check(0, at(0)).throttled, "来源未知时不限流");

    // 7) 条目上限：满了之后不再跟踪新 IP（fail-open），已有记录仍然生效
    IpThrottle::Config small = testConfig();
    small.maxEntries = 2;
    small.maxFailures = 2;
    IpThrottle::setConfigForTest(small);
    for (int i = 0; i < small.maxFailures; ++i) {
        IpThrottle::onFailure(ipA, at(0));
        IpThrottle::onFailure(ipB, at(0));
        IpThrottle::onFailure(0x0A000003, at(0));   // 第三个 IP：条目已满，不跟踪
    }
    check(IpThrottle::check(ipA, at(0)).throttled, "已有记录仍然生效");
    check(!IpThrottle::check(0x0A000003, at(0)).throttled, "条目满后不再跟踪新 IP");

    // 8) 全部过期后条目被清掉：不能靠"很久以前失败过"一直占内存
    check(!IpThrottle::check(ipA, at(5000)).throttled, "过期条目不再拦截");
    check(!IpThrottle::check(ipA, at(5000)).throttled, "连续查询也不会复活");

    // 9) 时间片边界：正好落在窗口边缘的失败按"滑出"处理，不会多算一片
    IpThrottle::Config tiny = testConfig();
    tiny.maxFailures = 2;
    IpThrottle::setConfigForTest(tiny);
    IpThrottle::onFailure(ipA, at(0));
    IpThrottle::onFailure(ipA, at(1000));
    check(IpThrottle::check(ipA, at(1000)).throttled, "两个时间片各一次即达阈值");
    check(!IpThrottle::check(ipA, at(6000)).throttled, "两个时间片都滑出窗口后放行");

    // 10) 日志格式
    check(IpThrottle::format(0x7F000001) == "127.0.0.1", "IPv4 格式化为点分十进制");
    check(IpThrottle::format(0) == "0.0.0.0", "未知来源格式化为 0.0.0.0");

    if (gFailures != 0) {
        std::printf("%d 项自检失败\n", gFailures);
        return 1;
    }
    std::printf("IP 兜底自检全部通过\n");
    return 0;
}
