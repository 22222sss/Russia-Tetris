"""最小协议 mock：用来在本地验证 tests/integration_test.py 的断言逻辑。

只实现集成测试会走到的分支，行为按 Russia-Tetris 服务端源码的文本拼出来。

密码部分刻意和真服务端保持一致：注册落盘写加盐哈希、登录按哈希校验，
遇到旧版明文账号则"登录成功后就地迁移"（见 migrate_credential），
否则集成测试里针对哈希/迁移的断言在 mock 上跑不通，就失去本地预检的意义。
"""
import math
import os
import socket
import sys
import threading
import time

import credential
import db

PORT = int(sys.argv[1])
DB_PATH = sys.argv[2]

MENU = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请选择操作："
        "\x1b[13;14H\x1b[37m1. 注册帐号"
        "\x1b[14;14H\x1b[37m2. 登录"
        "\x1b[16;14H\x1b[37m请选择操作：")
LOAD_MENU = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请选择操作："
             "\x1b[14;14H\x1b[37m1. 我最近的成绩"
             "\x1b[16;14H\x1b[37m2. 全服top成绩"
             "\x1b[18;14H\x1b[37m3. 开始游戏"
             "\x1b[20;14H\x1b[37m4. 返回服务"
             "\x1b[22;14H\x1b[37m5. 修改密码"
             "\x1b[24;14H\x1b[37m你的选择是：")
DIFF = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m1. 简单模式"
        "\x1b[14;14H\x1b[37m2. 普通模式"
        "\x1b[16;14H\x1b[37m3. 困难模式"
        "\x1b[18;14H\x1b[37m4. 返回上级菜单"
        "\x1b[20;14H\x1b[37m请选择操作：")
GAME_ON = "\x1b[2J\x1b[H\x1b[2;41H\x1b[37mNext:\x1b[14;42H\x1b[37mScore: "
OVER = "\x1b[12;14H\x1b[37mGAME OVER\r\n\x1b[15;14H\x1b[37mStart Again ? (y/n):"

def password_policy_ok(password):
    """与 src/TetrisGame/PasswordPolicy.h 保持一致（注册密码策略）。

    Python 的 len() 正好是 UTF-8 码点数，和 C++ 侧的 utf8Length 等价。
    只约束注册：老账号登录不受限。
    """
    return len(password) >= 8 and password.strip(" \t") != ""


# ---------------------------------------------------------------------------
# 登录失败退避：与 src/TetrisGame/AuthThrottle.cpp 对齐。
# 真服务端的状态只在事件循环线程访问、不用加锁；mock 是多线程的，这里用一把锁。
# ---------------------------------------------------------------------------
_throttle_lock = threading.Lock()
_throttle_entries = {}   # username -> (failures, last_failure, next_allowed)，时间是 monotonic


def _auth_env_int(name, default):
    try:
        value = int(os.environ.get(name, "") or default)
    except (TypeError, ValueError):
        return default
    return value if value >= 0 else default


def _auth_config():
    return (
        _auth_env_int("TETRIS_AUTH_FREE_ATTEMPTS", 5),
        _auth_env_int("TETRIS_AUTH_BASE_DELAY_MS", 1000),
        _auth_env_int("TETRIS_AUTH_MAX_DELAY_MS", 30000),
        _auth_env_int("TETRIS_AUTH_ENTRY_TTL_MS", 15 * 60 * 1000),
    )


def _auth_delay_ms(failures):
    free, base, max_delay, _ttl = _auth_config()
    if failures <= free:
        return 0
    exponent = min(failures - free - 1, 30)
    return min(base << exponent, max_delay)


def throttle_wait(username):
    """返回还要等几秒；0 表示不节流。"""
    now = time.monotonic()
    with _throttle_lock:
        entry = _throttle_entries.get(username)
        if entry is None:
            return 0
        _failures, last_failure, next_allowed = entry
        ttl = _auth_config()[3] / 1000.0
        if now - last_failure >= ttl:
            _throttle_entries.pop(username, None)
            return 0
        if now >= next_allowed:
            return 0
        return max(1, int(math.ceil(next_allowed - now)))


def throttle_failure(username):
    now = time.monotonic()
    ttl = _auth_config()[3] / 1000.0
    with _throttle_lock:
        failures, last_failure, _next = _throttle_entries.get(username, (0, 0.0, 0.0))
        if now - last_failure >= ttl:
            failures = 0
        failures += 1
        _throttle_entries[username] = (failures, now, now + _auth_delay_ms(failures) / 1000.0)


def throttle_success(username):
    with _throttle_lock:
        _throttle_entries.pop(username, None)


# ---------------------------------------------------------------------------
# 按来源 IP 的粗粒度兜底：与 src/TetrisGame/IpThrottle.cpp 对齐。
# 5 个等长槽位组成滑动窗口；被拒绝的尝试不记账、不延长窗口；成功登录不清零
# （注册是开放的，成功后清零等于给攻击者一个免费的重置开关）。
# 真服务端只在事件循环线程上访问，不用加锁；mock 是多线程的，这里用一把锁。
# ---------------------------------------------------------------------------
IP_SLOTS = 5

_ip_lock = threading.Lock()
_ip_entries = {}   # ip -> [(时间片编号, 失败次数)] * IP_SLOTS


def _ip_config():
    return (
        _auth_env_int("TETRIS_AUTH_IP_MAX_FAILURES", 300),
        _auth_env_int("TETRIS_AUTH_IP_WINDOW_MS", 5 * 60 * 1000),
        _auth_env_int("TETRIS_AUTH_IP_MAX_ENTRIES", 4096),
    )


def _ip_slot_ms(window_ms):
    return max(1, window_ms // IP_SLOTS)


def ip_throttle_wait(peer_ip):
    """返回还要等几秒；0 表示不节流。"""
    max_failures, window_ms, _max_entries = _ip_config()
    slot_ms = _ip_slot_ms(window_ms)
    now_ms = int(time.monotonic() * 1000)
    cur = now_ms // slot_ms
    with _ip_lock:
        slots = _ip_entries.get(peer_ip)
        if slots is None:
            return 0
        total = sum(count for bucket, count in slots
                    if bucket is not None and bucket <= cur and cur - bucket < IP_SLOTS)
        if total == 0:
            _ip_entries.pop(peer_ip, None)
            return 0
        if total < max_failures:
            return 0
        # 等最老的失败滑出窗口（和 C++ 侧同一算法）
        acc = total
        drain_ms = (cur + 1) * slot_ms
        for bucket, count in sorted(b for b in slots if b[0] is not None):
            if bucket > cur or cur - bucket >= IP_SLOTS:
                continue
            acc -= count
            if acc < max_failures:
                drain_ms = (bucket + IP_SLOTS) * slot_ms
                break
        return max(1, int(math.ceil((drain_ms - now_ms) / 1000.0)))


def ip_throttle_failure(peer_ip):
    _max_failures, window_ms, max_entries = _ip_config()
    slot_ms = _ip_slot_ms(window_ms)
    cur = int(time.monotonic() * 1000) // slot_ms
    index = cur % IP_SLOTS
    with _ip_lock:
        slots = _ip_entries.get(peer_ip)
        if slots is None:
            if len(_ip_entries) >= max_entries:
                return      # 条目满了不再跟踪新 IP（fail-open）
            slots = [(None, 0)] * IP_SLOTS
            _ip_entries[peer_ip] = slots
        bucket, count = slots[index]
        if bucket != cur:
            slots[index] = (cur, 1)
        else:
            slots[index] = (cur, count + 1)


def account_exists(name):
    """账号是否存在（直接查 SQLite，多个 mock 进程共享同一个库文件）。"""
    return db.account_exists(DB_PATH, name)


def stored_credential(name):
    """返回存储的密码字段（明文或哈希）；账号不存在返回 None。"""
    return db.read_credential(DB_PATH, name)


def write_account(name, password):
    """注册：只存加盐哈希（与真服务端一致）。"""
    try:
        db.insert_account(DB_PATH, name, credential.hash_password(password))
    except Exception as exc:
        print("db error:", exc)
        sys.exit(1)


def migrate_credential(name, password):
    """模拟服务端"旧明文账号登录成功后迁移成加盐哈希"。"""
    db.update_credential(DB_PATH, name, credential.hash_password(password))


def serve(conn):
    try:
        peer_ip = conn.getpeername()[0]
    except OSError:
        peer_ip = "0.0.0.0"
    conn.sendall(MENU.encode())
    state = "menu"
    buf = b""
    downs = 0
    pending_user = ""
    pending_old = ""
    try:
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                return
            buf += chunk

            if state in ("playing", "over"):
                if state == "playing":
                    downs += chunk.count(b"\x1b[B")
                    if downs >= 30:
                        conn.sendall(OVER.encode())
                        state = "over"
                else:
                    if b"n" in chunk or b"N" in chunk:
                        conn.sendall(MENU.encode())
                        state = "menu"
                continue

            while b"\r\n" in buf:
                line, buf = buf.split(b"\r\n", 1)
                cmd = line.decode("utf-8", "replace")
                if state == "menu":
                    if cmd == "1":
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请输入用户名：".encode())
                        state = "reg_user"
                    elif cmd == "2":
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请输入用户名：".encode())
                        state = "login_user"
                    else:
                        conn.sendall(MENU.encode())
                elif state == "reg_user":
                    if account_exists(cmd):
                        conn.sendall("该用户名已被注册，请选择其他用户名:".encode())
                    else:
                        conn.sendall("\x1b[15;14H\x1b[37m请输入密码：".encode())
                        state = "reg_pwd"
                        pending_user = cmd
                elif state == "reg_pwd":
                    # 与真服务端一致：密码不满足策略时提示并留在密码输入状态，
                    # 否则集成测试里的"短密码被拒"分支在本地 mock 上跑不通。
                    if not password_policy_ok(cmd):
                        conn.sendall("\x1b[12;14H\x1b[37m密码至少需要 8 个字符（不能全是空格），"
                                     "请重新输入密码：".encode())
                        continue
                    write_account(pending_user, cmd)
                    conn.sendall("\x1b[16;14H\x1b[37m注册成功！\r\n"
                                 "\x1b[17;14H\x1b[37m请按3返回上级菜单：".encode())
                    state = "after_reg"
                elif state == "after_reg":
                    conn.sendall(MENU.encode())
                    state = "menu"
                elif state == "login_user":
                    pending_user = cmd
                    conn.sendall("\x1b[15;14H\x1b[37m请输入密码：".encode())
                    state = "login_pwd"
                elif state == "login_pwd":
                    # 先判 IP（与真服务端同序）：换用户名喷洒时只有这一层拦得住
                    wait = ip_throttle_wait(peer_ip)
                    if wait == 0:
                        wait = throttle_wait(pending_user)
                    if wait > 0:
                        conn.sendall(("\x1b[12;14H\x1b[37m登录尝试过于频繁，请 %d 秒后再试：\r\n"
                                      "\x1b[14;14H\x1b[37m请输入密码：" % wait).encode())
                        continue        # 保持"输入密码"状态，等够时间可重试
                    stored = stored_credential(pending_user)
                    if stored is not None and credential.verify(cmd, stored):
                        throttle_success(pending_user)
                        # 旧版明文账号：登录成功后迁移（与真服务端一致）
                        if not credential.is_hashed(stored):
                            migrate_credential(pending_user, cmd)
                        conn.sendall("登录中...\r\n".encode())
                        conn.sendall(LOAD_MENU.encode())
                        state = "load_menu"
                    else:
                        throttle_failure(pending_user)
                        ip_throttle_failure(peer_ip)
                        conn.sendall("登录失败，用户名或密码错误。\r\n按3返回上级菜单:".encode())
                        state = "after_fail"
                elif state == "after_fail":
                    conn.sendall(MENU.encode())
                    state = "menu"
                elif state == "load_menu":
                    if cmd == "1":
                        conn.sendall("暂无成绩记录".encode())
                        state = "score_view"
                    elif cmd == "2":
                        conn.sendall("简单模式：\r\n用户名 分数 用户获取最高分的时间\r\n"
                                     "请按3返回菜单：".encode())
                        state = "top_view"
                    elif cmd == "3":
                        conn.sendall(DIFF.encode())
                        state = "diff"
                    elif cmd == "5":
                        # 修改密码（与真服务端一致：先收当前密码，再收新密码，
                        # 两个都收齐了才校验——所以"当前密码错"要等新密码也发完才提示）
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m修改密码"
                                     "\x1b[14;14H\x1b[37m请输入当前密码：".encode())
                        state = "change_old"
                    else:
                        conn.sendall(MENU.encode())
                        state = "menu"
                elif state == "change_old":
                    pending_old = cmd
                    conn.sendall("\x1b[16;14H\x1b[37m"
                                 "请输入新密码（至少 8 个字符，不能全是空格）：".encode())
                    state = "change_new"
                elif state == "change_new":
                    if not password_policy_ok(cmd):
                        conn.sendall("\x1b[12;14H\x1b[37m密码至少需要 8 个字符（不能全是空格），"
                                     "请重新输入密码：".encode())
                        continue
                    if cmd == pending_old:
                        conn.sendall("\x1b[12;14H\x1b[37m新密码不能与当前密码相同，"
                                     "请重新输入新密码：".encode())
                        continue
                    wait = ip_throttle_wait(peer_ip)
                    if wait == 0:
                        wait = throttle_wait(pending_user)
                    if wait > 0:
                        conn.sendall(("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m"
                                      "修改密码尝试过于频繁，请 %d 秒后再试：\r\n"
                                      "\x1b[14;14H\x1b[37m请按3返回主菜单：" % wait).encode())
                        state = "after_change"
                        continue
                    stored = stored_credential(pending_user)
                    if stored is not None and credential.verify(pending_old, stored):
                        db.update_credential(DB_PATH, pending_user, credential.hash_password(cmd))
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m密码修改成功！\r\n"
                                     "\x1b[13;14H\x1b[37m下次登录请使用新密码。\r\n"
                                     "\x1b[15;14H\x1b[37m请按3返回主菜单：".encode())
                        state = "after_change"
                    else:
                        throttle_failure(pending_user)
                        ip_throttle_failure(peer_ip)
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m"
                                     "当前密码错误，密码未修改。\r\n"
                                     "\x1b[14;14H\x1b[37m请按3返回主菜单：".encode())
                        state = "after_change"
                elif state == "after_change":
                    conn.sendall(LOAD_MENU.encode())
                    state = "load_menu"
                elif state in ("score_view", "top_view"):
                    conn.sendall(LOAD_MENU.encode())
                    state = "load_menu"
                elif state == "diff":
                    if cmd in ("1", "2", "3"):
                        conn.sendall(GAME_ON.encode())
                        state = "playing"
                        downs = 0
                    else:
                        conn.sendall(LOAD_MENU.encode())
                        state = "load_menu"
    except OSError:
        return
    finally:
        try:
            conn.close()
        except OSError:
            pass


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(64)
    print("mock listening on", PORT, flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
