#include "PasswordHash.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdexcept>
#include <vector>

namespace {

const char* const kAlgoTag = "pbkdf2-sha256";
// 完整前缀（含首尾 '$'）：用来区分"哈希"和"旧版明文"
const char* const kHashedPrefix = "$pbkdf2-sha256$";

std::string toHex(const unsigned char* data, std::size_t len) {
    static const char* const kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& text, std::vector<unsigned char>& out) {
    if (text.empty() || text.size() % 2 != 0) return false;
    out.clear();
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        int hi = hexDigit(text[i]);
        int lo = hexDigit(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

bool derive(const std::string& password, const unsigned char* salt, std::size_t saltLen,
            int iterations, unsigned char* out, std::size_t outLen) {
    // OpenSSL 的这些参数是 int：先挡住异常输入，避免窄化溢出
    if (password.size() > 4096 || saltLen > 256 || outLen > 256) return false;
    return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                             salt, static_cast<int>(saltLen), iterations,
                             EVP_sha256(), static_cast<int>(outLen), out) == 1;
}

bool constantTimeEquals(const unsigned char* a, const unsigned char* b, std::size_t len) {
    return CRYPTO_memcmp(a, b, len) == 0;
}

bool isHashedPrefix(const std::string& stored) {
    return stored.compare(0, std::char_traits<char>::length(kHashedPrefix), kHashedPrefix) == 0;
}

struct ParsedCredential {
    int iterations = 0;
    std::vector<unsigned char> salt;
    std::vector<unsigned char> hash;
};

// 解析 "$pbkdf2-sha256$<iter>$<salt-hex>$<hash-hex>"。
// 任何一处不合法都返回 false（此时上层不会退回明文比较）。
bool parseCredential(const std::string& stored, ParsedCredential& parsed) {
    if (!isHashedPrefix(stored)) return false;

    std::vector<std::string> parts;
    std::size_t start = 1;   // 跳过开头的 '$'
    while (true) {
        std::size_t pos = stored.find('$', start);
        if (pos == std::string::npos) {
            parts.push_back(stored.substr(start));
            break;
        }
        parts.push_back(stored.substr(start, pos - start));
        start = pos + 1;
    }
    // 期望 4 段：算法名、迭代次数、盐、摘要
    if (parts.size() != 4 || parts[0] != kAlgoTag) return false;

    if (parts[1].empty() || parts[1].size() > 9) return false;
    int iterations = 0;
    for (char c : parts[1]) {
        if (c < '0' || c > '9') return false;
        iterations = iterations * 10 + (c - '0');
    }
    if (iterations < 1 || iterations > 10000000) return false;

    if (!fromHex(parts[2], parsed.salt) || !fromHex(parts[3], parsed.hash)) return false;
    if (parsed.salt.size() < 8 || parsed.salt.size() > 64) return false;
    if (parsed.hash.size() != PasswordHash::kHashBytes) return false;

    parsed.iterations = iterations;
    return true;
}

}  // namespace

std::string PasswordHash::hash(const std::string& password) {
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("生成密码盐失败（RAND_bytes）");
    }

    std::vector<unsigned char> digest(kHashBytes);
    if (!derive(password, salt.data(), salt.size(), kIterations,
                digest.data(), digest.size())) {
        throw std::runtime_error("PBKDF2 计算失败");
    }

    std::string out = kHashedPrefix;
    out += std::to_string(kIterations);
    out += '$';
    out += toHex(salt.data(), salt.size());
    out += '$';
    out += toHex(digest.data(), digest.size());
    return out;
}

bool PasswordHash::isHashed(const std::string& stored) {
    return isHashedPrefix(stored);
}

bool PasswordHash::verify(const std::string& password, const std::string& stored,
                          bool* needsRehash) {
    if (needsRehash) *needsRehash = false;

    ParsedCredential parsed;
    if (parseCredential(stored, parsed)) {
        std::vector<unsigned char> digest(kHashBytes);
        if (!derive(password, parsed.salt.data(), parsed.salt.size(), parsed.iterations,
                    digest.data(), digest.size())) {
            return false;
        }
        if (!constantTimeEquals(digest.data(), parsed.hash.data(), kHashBytes)) {
            return false;
        }
        // 参数落后于当前实现时顺手升级（下次登录会重新哈希）
        if (needsRehash &&
            (parsed.iterations != kIterations || parsed.salt.size() != kSaltBytes)) {
            *needsRehash = true;
        }
        return true;
    }

    // 前缀是哈希但格式非法：直接拒绝。绝不能退回明文比较——
    // 否则改坏一条记录就能绕过校验。
    if (isHashedPrefix(stored)) {
        return false;
    }

    // 旧版明文密码（迁移前遗留的数据）
    if (stored.empty() || stored.size() != password.size()) {
        return false;
    }
    if (!constantTimeEquals(reinterpret_cast<const unsigned char*>(stored.data()),
                            reinterpret_cast<const unsigned char*>(password.data()),
                            stored.size())) {
        return false;
    }
    if (needsRehash) *needsRehash = true;
    return true;
}
