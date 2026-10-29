#!/usr/bin/env python3
"""跨进程存储一致性测试：两个服务进程共享同一份 userdata.csv。

验证存储层的多进程安全（flock 互斥 + 缓存按文件状态失效）：
在 A 进程注册的账号，必须能在 B 进程登录成功，反之亦然。

用法（两个进程的工作目录必须相同，才会共享 userdata.csv）：

    TETRIS_PORT=9999 ./tetris_game &
    TETRIS_PORT=9998 ./tetris_game &
    python3 tests/cross_process_test.py --port-a 9999 --port-b 9998

复用 tests/integration_test.py 里的连接与断言工具。
"""

import argparse
import random
import sys
import time

import integration_test as it


STAGE = "启动"


def expect(conn, needle, timeout=None):
    if conn.wait_for(needle, timeout):
        return
    raise it.Failure("[%s] 未等到 %r；最近输出：%r" % (STAGE, needle, conn.tail()))


def register(port, user, password, userdata):
    global STAGE
    STAGE = "在端口 %d 注册 %s" % (port, user)

    conn = it.Conn("127.0.0.1", port)
    try:
        expect(conn, "1. 注册帐号")
        conn.sendline("1")
        expect(conn, "请输入用户名")
        conn.sendline(user)
        expect(conn, "请输入密码")
        conn.sendline(password)
        expect(conn, "注册成功")
    finally:
        conn.close()

    # 落盘是异步的：等文件里真的出现这个账号，再去另一个进程登录，
    # 否则会测成"写盘还没完成"的竞态，而不是存储层本身。
    STAGE = "等待 %s 落盘" % user
    if not it.wait_for_userdata(userdata, user, 15.0):
        raise it.Failure("[%s] userdata.csv 里没找到 %r" % (STAGE, user))


def login(port, user, password, note):
    global STAGE
    STAGE = "%s（端口 %d）" % (note, port)

    conn = it.Conn("127.0.0.1", port)
    try:
        expect(conn, "1. 注册帐号")
        conn.sendline("2")
        expect(conn, "请输入用户名")
        conn.sendline(user)
        expect(conn, "请输入密码")
        conn.sendline(password)
        expect(conn, "3. 开始游戏")
    finally:
        conn.close()


def login_must_fail(port, user, note):
    """反向校验：不存在的账号必须被拒绝，确认不是"什么都放行"。"""
    global STAGE
    STAGE = "%s（端口 %d）" % (note, port)

    conn = it.Conn("127.0.0.1", port)
    try:
        expect(conn, "1. 注册帐号")
        conn.sendline("2")
        expect(conn, "请输入用户名")
        conn.sendline(user)
        expect(conn, "请输入密码")
        conn.sendline(user + "-wrong")
        expect(conn, "登录失败")
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description="跨进程 userdata.csv 一致性测试")
    parser.add_argument("--port-a", type=int, required=True)
    parser.add_argument("--port-b", type=int, required=True)
    parser.add_argument("--userdata", default="userdata.csv",
                        help="CSV 路径（相对服务端工作目录）")
    args = parser.parse_args()

    suffix = "%d%d" % (int(time.time()) % 100000, random.randint(100, 999))
    user_a = "xpA" + suffix
    user_b = "xpB" + suffix
    pwd_a = "pa" + str(random.randint(100000, 999999))
    pwd_b = "pb" + str(random.randint(100000, 999999))
    print("测试账号: %s / %s" % (user_a, user_b), flush=True)

    checks = [
        ("A 注册 -> B 登录", lambda: (
            register(args.port_a, user_a, pwd_a, args.userdata),
            login(args.port_b, user_a, pwd_a, "另一个进程登录 A 注册的账号"),
        )),
        ("B 注册 -> A 登录", lambda: (
            register(args.port_b, user_b, pwd_b, args.userdata),
            login(args.port_a, user_b, pwd_b, "另一个进程登录 B 注册的账号"),
        )),
        ("不存在的账号必须被拒绝", lambda: login_must_fail(
            args.port_a, "nobody" + suffix, "拒绝不存在的账号")),
    ]

    failed = 0
    for name, fn in checks:
        started = time.time()
        try:
            fn()
        except (it.Failure, OSError) as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            it.annotate("error", "跨进程测试失败 - %s: %s" % (name, exc))
        else:
            print("PASS %s (%.1fs)" % (name, time.time() - started), flush=True)

    if failed:
        print("跨进程测试失败: %d/%d 项" % (failed, len(checks)), flush=True)
        return 1

    print("跨进程存储一致性测试通过: %d/%d 项" % (len(checks), len(checks)), flush=True)
    it.annotate("notice", "跨进程存储一致性通过（%d 项）" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
