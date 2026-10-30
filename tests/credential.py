"""密码凭据格式的 Python 侧实现（与 src/TetrisGame/PasswordHash.cpp 保持一致）。

存储格式：$pbkdf2-sha256$<迭代次数>$<盐 hex>$<摘要 hex>

用途：
1. tests/mock_server.py 用它模拟"注册存哈希、登录校验、旧明文登录即迁移"，
   这样集成测试脚本可以完全在本地（无 C++/WSL）验证；
2. tests/integration_test.py 用它复算服务端落盘的哈希，确认存的确实是
   "这个密码的 PBKDF2"，而不只是"看起来像哈希"；
3. tests/play_test.py --seed 直接造哈希账号，避免压测时触发 200 次迁移。

只依赖标准库。
"""

import hashlib
import hmac
import secrets

ALGO = "pbkdf2-sha256"
PREFIX = "$" + ALGO + "$"
DEFAULT_ITERATIONS = 100000
SALT_BYTES = 16
HASH_BYTES = 32


def hash_password(password, iterations=DEFAULT_ITERATIONS, salt=None):
    """生成加盐哈希，返回服务端存储格式的字符串。"""
    if salt is None:
        salt = secrets.token_bytes(SALT_BYTES)
    if isinstance(password, str):
        password = password.encode("utf-8")
    digest = hashlib.pbkdf2_hmac("sha256", password, salt, iterations, HASH_BYTES)
    return "%s%d$%s$%s" % (PREFIX, iterations, salt.hex(), digest.hex())


def is_hashed(stored):
    return isinstance(stored, str) and stored.startswith(PREFIX)


def parse(stored):
    """解析成 (iterations, salt, digest)；格式不合法返回 None。"""
    if not is_hashed(stored):
        return None
    parts = stored.split("$")
    # ["", "pbkdf2-sha256", iterations, salt, digest]
    if len(parts) != 5 or parts[0] != "" or parts[1] != ALGO:
        return None
    try:
        iterations = int(parts[2])
        salt = bytes.fromhex(parts[3])
        digest = bytes.fromhex(parts[4])
    except ValueError:
        return None
    if not (1 <= iterations <= 10000000):
        return None
    if not (8 <= len(salt) <= 64) or len(digest) != HASH_BYTES:
        return None
    return iterations, salt, digest


def verify(password, stored):
    """校验 password 与存储值是否匹配（支持旧版明文）。"""
    if isinstance(password, str):
        password_bytes = password.encode("utf-8")
    else:
        password_bytes = password

    parsed = parse(stored)
    if parsed is not None:
        iterations, salt, digest = parsed
        candidate = hashlib.pbkdf2_hmac("sha256", password_bytes, salt, iterations, HASH_BYTES)
        return hmac.compare_digest(candidate, digest)

    # 前缀像哈希但格式坏了：拒绝，绝不退回明文比较
    if is_hashed(stored):
        return False

    # 旧版明文密码
    if not stored:
        return False
    return hmac.compare_digest(stored.encode("utf-8"), password_bytes)
