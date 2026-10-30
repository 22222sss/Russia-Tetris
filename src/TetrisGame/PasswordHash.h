#pragma once
#ifndef PasswordHash_H
#define PasswordHash_H

#include <cstddef>
#include <string>

// 密码加盐哈希：PBKDF2-HMAC-SHA256（OpenSSL 实现）。
//
// 存储格式（userdata.csv 的 Password 列）：
//     $pbkdf2-sha256$<迭代次数>$<盐 hex>$<摘要 hex>
// 例：$pbkdf2-sha256$100000$1f3c...$9ab2...
//
// 用 hex 而不是 base64：不需要额外编码库，且绝不会出现逗号——
// userdata.csv 是按逗号裸切的，字段里出现逗号会直接破坏整行。
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
};

#endif
