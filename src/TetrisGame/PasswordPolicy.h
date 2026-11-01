#pragma once
#ifndef PasswordPolicy_H
#define PasswordPolicy_H

#include <cstddef>
#include <string>

// 注册密码策略。**只约束新注册**，不约束登录——老账号的短密码不能被锁在门外
// （否则升级一版就可能把人挡在外面）。
//
// 按 UTF-8 码点而不是字节数：中文密码按字节算 3 字节/字，3 个汉字就会"通过" 8 字节，
// 但用户实际只输 3 个字符，不符合直觉。
namespace PasswordPolicy {

constexpr std::size_t kMinLength = 8;

// UTF-8 码点个数（跳过 continuation 字节；非法 UTF-8 只会少数，不影响安全判断）
inline std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

// 全是空格/Tab 的密码等于没有密码，一并拒绝
inline bool isBlank(const std::string& text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}

// 新注册密码是否满足策略。失败提示见 UImanage::show_Password_Too_Short_Error。
inline bool check(const std::string& password) {
    return utf8Length(password) >= kMinLength && !isBlank(password);
}

}  // namespace PasswordPolicy

#endif
