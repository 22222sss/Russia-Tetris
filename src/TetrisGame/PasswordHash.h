#pragma once
#ifndef PasswordHash_H
#define PasswordHash_H

#include <cstddef>
#include <string>

// 密码加盐哈希：PBKDF2-HMAC-SHA256（OpenSSL 实现）。
//
// 存储格式（SQLite accounts.password）：
//     $pbkdf2-sha256$<迭代次数>$<盐 hex>$<摘要 hex>
// 例：$pbkdf2-sha256$100000$1f3c...$9ab2...
//
// 用 hex 而不是 base64：不需要额外编码库，也没有分隔符风险
// （历史上这列存在 CSV 里，是按逗号裸切的；现在虽然进了 SQLite，格式保持不变）。
//
// 旧版本存的是明文密码；verify() 对明文做兼容匹配并置 needsRehash，
// 由调用方在登录成功后就地迁移成哈希（见 Filedata::loginAsync）。
class PasswordHash {
public:
    // 迭代次数是"安全 vs 登录延迟"的旋钮：本机实测约 0.5ms/万次，
    // 10 万次约 50ms。校验放在异步线程做，不占事件循环线程（见 Server）。
    static constexpr int kIterations = 100000;
    static constexpr std::size_t kSaltBytes = 16;
    static constexpr std::size_t kHashBytes = 32;

    // 生成新的加盐哈希。RAND_bytes 失败或 PBKDF2 失败时抛 std::runtime_error。
    static std::string hash(const std::string& password);

    // 是否是本文档定义的哈希格式（只看前缀；格式非法时 verify 会拒绝）。
    static bool isHashed(const std::string& stored);

    // 校验 password 与 stored 是否匹配。
    // needsRehash：命中但存储格式需要升级（旧明文，或迭代次数/盐长度与当前参数不同）时为 true。
    static bool verify(const std::string& password, const std::string& stored,
                       bool* needsRehash = nullptr);

    // 用一条"谁也不知道密码"的随机假凭据做一次完整校验，返回值恒为 false。
    //
    // 用途：登录时账号不存在也要付出和真实校验相同的 PBKDF2 代价，
    // 否则"响应很快"本身就等于告诉客户端"这个用户名不存在"，可被用来枚举账号。
    // 假凭据在首次调用时用 RAND_bytes 生成一次（线程安全），参数与真实凭据一致。
    static bool verifyDummy(const std::string& password);
};

#endif
