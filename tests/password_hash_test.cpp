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

    // 6) 顺带给出实际耗时（CI 注解里能看到；也用于判断迭代次数是否合适）
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
