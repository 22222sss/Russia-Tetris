// PasswordHash 自检：在 CI 里编译运行（见 .github/workflows/build.yml）。
//
// 为什么单独测：集成测试只能看到"注册/登录通不通"，如果哈希实现细节错了
// （比如盐没进 PBKDF2、hex 编解码错位），很可能仍然自洽地通过。这里用
// Python hashlib 生成的已知向量做交叉验证，并覆盖"旧明文迁移"和"坏记录拒绝"。
//
// 编译运行（在 src/ 下）：
//   g++ -std=c++17 -Wall -Wextra -I. TetrisGame/PasswordHash.cpp \
//       ../tests/password_hash_test.cpp -lcrypto -pthread -o /tmp/ph_test && /tmp/ph_test

#include "TetrisGame/PasswordHash.h"
#include "TetrisGame/PasswordPolicy.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++gFailures;
    }
}

// Python: hashlib.pbkdf2_hmac("sha256", b"test-vector-password", bytes(range(16)), 1000, 32)
const char* const kKnownVector =
    "$pbkdf2-sha256$1000$000102030405060708090a0b0c0d0e0f$"
    "a98329702f7f64758b28b1e5af5a8f07920e2bd97bc49239d5ab8f81ae040202";

}  // namespace

int main() {
    // 1) 生成格式：前缀 + 4 段 + 迭代次数
    const std::string generated = PasswordHash::hash("s3cret-pw");
    check(generated.rfind("$pbkdf2-sha256$", 0) == 0, "生成的哈希带算法前缀");
    check(generated.find("$pbkdf2-sha256$100000$") == 0, "生成的哈希记录当前迭代次数");

    size_t separators = 0;
    for (char c : generated) {
        if (c == '$') ++separators;
    }
    check(separators == 4, "生成的哈希是 4 段（3 个分隔符 + 开头）");

    check(PasswordHash::verify("s3cret-pw", generated), "正确密码通过校验");
    check(!PasswordHash::verify("s3cret-pW", generated), "错误密码被拒绝");

    // 2) 盐是随机的：同一密码两次哈希必须不同，但都能校验
    const std::string generated2 = PasswordHash::hash("s3cret-pw");
    check(generated != generated2, "同一密码两次哈希不同（随机盐）");
    check(PasswordHash::verify("s3cret-pw", generated2), "第二份哈希同样可校验");

    // 3) 与 Python hashlib 的已知向量交叉验证（PBKDF2 参数 + hex 编解码）
    check(PasswordHash::verify("test-vector-password", kKnownVector),
          "Python hashlib 已知向量校验通过");
    check(!PasswordHash::verify("test-vector-passworD", kKnownVector),
          "已知向量的错误密码被拒绝");

    // 4) 旧版明文：匹配成功且要求迁移
    bool needsRehash = false;
    check(PasswordHash::verify("legacy-pw", "legacy-pw", &needsRehash) && needsRehash,
          "旧明文密码匹配并标记需要迁移");
    needsRehash = true;
    check(!PasswordHash::verify("legacy-pw", "other-pw", &needsRehash) && !needsRehash,
          "旧明文密码不匹配时被拒绝");
    check(!PasswordHash::verify("legacy-pw", "", &needsRehash),
          "空存储值不会匹配任何密码");

    // 5) 坏记录不能退回明文比较
    check(!PasswordHash::verify("abc", "$pbkdf2-sha256$notanumber$zz$zz"),
          "非法迭代次数被拒绝");
    check(!PasswordHash::verify("abc", "$pbkdf2-sha256$1000$000102030405060708090a0b0c0d0e0f$00"),
          "摘要长度不对被拒绝");
    check(!PasswordHash::verify("abc", "$pbkdf2-sha256$1000$00$"
                                      "a98329702f7f64758b28b1e5af5a8f07920e2bd97bc49239d5ab8f81ae040202"),
          "盐过短被拒绝");

    // 6) verifyDummy：账号不存在时也要付出等价代价（消除"响应快=账号不存在"的时序侧信道）
    PasswordHash::verifyDummy("warmup");   // 第一次调用会额外生成随机假凭据，先预热
    check(!PasswordHash::verifyDummy("any-password-123"), "verifyDummy 恒返回 false");
    check(!PasswordHash::verifyDummy(""), "verifyDummy 对空密码也返回 false");

    // 假校验不能是空操作：耗时应与真实校验同量级。每边跑 3 次取最小值，
    // 减少共享 runner 上被抢占带来的抖动；上下界放宽到 0.3x~3x，只抓
    // "根本没跑 PBKDF2"这类退化，不追求精确计时。
    long long realUs = -1;
    long long dummyUs = -1;
    for (int i = 0; i < 3; ++i) {
        const auto realStart = std::chrono::steady_clock::now();
        PasswordHash::hash("timing-baseline");
        const long long realRound = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - realStart).count();
        if (realUs < 0 || realRound < realUs) realUs = realRound;

        const auto dummyStart = std::chrono::steady_clock::now();
        PasswordHash::verifyDummy("timing-baseline");
        const long long dummyRound = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - dummyStart).count();
        if (dummyUs < 0 || dummyRound < dummyUs) dummyUs = dummyRound;
    }
    check(dummyUs * 10 >= realUs * 3 && dummyUs <= realUs * 3,
          "verifyDummy 耗时与真实校验同量级");
    std::printf("timing: verifyDummy=%lldus, hash=%lldus (best of 3)\n", dummyUs, realUs);

    // 7) 注册密码策略（按 UTF-8 码点计数；只拦"太短"和"全空白"）
    check(PasswordPolicy::check("12345678"), "8 位 ASCII 密码通过策略");
    check(!PasswordPolicy::check("1234567"), "7 位 ASCII 密码被拒绝");
    check(!PasswordPolicy::check("        "), "全空格密码被拒绝");
    check(PasswordPolicy::check("密码密码密码密码"), "8 个汉字（24 字节）通过策略");
    check(!PasswordPolicy::check("密码密码"), "2 个汉字（6 字节）被拒绝");
    check(PasswordPolicy::check("  abcdefgh  "), "首尾带空格的 8 字符密码通过（只拦全空白）");

    // 8) 顺带给出实际耗时（CI 注解里能看到；也用于判断迭代次数是否合适）
    const auto started = std::chrono::steady_clock::now();
    const int rounds = 3;
    for (int i = 0; i < rounds; ++i) {
        PasswordHash::hash("timing-password");
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("timing: %lld ms/hash (iterations=%d)\n",
                static_cast<long long>(elapsedMs) / rounds, PasswordHash::kIterations);

    if (gFailures != 0) {
        std::printf("%d 项自检失败\n", gFailures);
        return 1;
    }
    std::printf("密码哈希自检全部通过\n");
    return 0;
}
