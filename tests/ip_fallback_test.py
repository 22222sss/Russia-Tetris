#!/usr/bin/env python3
"""按 IP 兜底的协议级测试（需要一个**低阈值**启动的服务端）。

为什么不能只靠 C++ 自检：`tests/ip_throttle_test.cpp` 只验证状态机；
"accept 时真的记下了来源 IP""拒绝发生在 PBKDF2 之前""拦的是 IP 而不是用户名"
这三件事只有走真实协议才能证明。所以这个脚本要配合低阈值服务端跑（见下）。

用法（在服务端的工作目录下执行，userdata.csv 就在那儿）：
    TETRIS_AUTH_IP_MAX_FAILURES=5 TETRIS_AUTH_IP_WINDOW_MS=10000 TETRIS_PORT=10006 \\
        ./tetris_game &
    python3 ../tests/ip_fallback_test.py --port 10006 --max-failures 5 --window-ms 10000

断言：
1. 用 N 个**不同**的用户名各失败一次（每个用户名只有 1 次失败，远不到
   AuthThrottle 的 5 次免延迟）——第 N 次之后该 IP 被拦，证明不是按用户名计数；
2. 被拦之后即使送**正确密码**也进不去（IP 检查在读凭据/跑 PBKDF2 之前）；
3. 同时从另一个源地址（默认 127.0.0.2）用同一账号能正常登录——证明是按 IP 隔离，
   不是"有人失败就全站封"；
4. 窗口滑过后同一 IP 恢复登录——证明不会永久锁死。
"""

import argparse
import random
import re
import sys
import time

import integration_test as it


def register_account(args, conn, user, password):
    it.STAGE = "注册测试账号"
    it.expect(conn, "1. 注册帐号")
    conn.sendline("1")
    it.expect(conn, "请输入用户名")
    conn.sendline(user)
    it.expect(conn, "请输入密码")
    conn.sendline(password)
    it.expect(conn, "注册成功")
    # 注册是异步落盘的：不等到落盘就登录，会偶发"登录失败"，把用例变成假失败
    if not it.wait_for_userdata(args.userdata, user, args.timeout):
        raise it.Failure("[%s] 账号 %r 未落盘（超时 %ss）" % (it.STAGE, user, args.timeout))
    conn.sendline("3")
    it.expect(conn, "2. 登录")


def fail_login_with_new_username(args, conn, index):
    """换一个不存在的用户名失败一次：用户名维度只记 1 次（不会触发用户退避）。"""
    it.STAGE = "第 %d 次失败（换新用户名，用户名维度只有 1 次）" % index
    conn.sendline("2")
    it.expect(conn, "请输入用户名")
    conn.sendline("nosuch-%d-%d" % (random.randint(100000, 999999), index))
    it.expect(conn, "请输入密码")
    conn.sendline("wrong-password-%d" % index)
    it.expect(conn, "登录失败")
    conn.sendline("3")          # 回主菜单，准备下一次
    it.expect(conn, "2. 登录")


def main():
    parser = argparse.ArgumentParser(description="按 IP 兜底的协议级测试")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=10006)
    parser.add_argument("--userdata", default="userdata.csv")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--max-failures", type=int, default=5,
                        help="必须与服务端 TETRIS_AUTH_IP_MAX_FAILURES 一致")
    parser.add_argument("--window-ms", type=int, default=10000,
                        help="必须与服务端 TETRIS_AUTH_IP_WINDOW_MS 一致")
    parser.add_argument("--source-ip", default="127.0.0.1",
                        help="用来触发限流的源地址（默认本机回环）")
    parser.add_argument("--other-source-ip", default="127.0.0.2",
                        help="用来验证按 IP 隔离的另一个源地址")
    args = parser.parse_args()

    user = it.new_account_name() + "ip"
    password = "pw" + str(random.randint(100000, 999999))
    print("测试账号: %s（阈值 %d 次/%dms）"
          % (user, args.max_failures, args.window_ms), flush=True)

    started = time.time()
    conn = it.Conn(args.host, args.port, args.timeout, source_ip=args.source_ip)
    try:
        register_account(args, conn, user, password)

        for i in range(1, args.max_failures + 1):
            fail_login_with_new_username(args, conn, i)

        it.STAGE = "达到阈值后：正确密码也应被拒绝"
        conn.sendline("2")
        it.expect(conn, "请输入用户名")
        conn.sendline(user)
        it.expect(conn, "请输入密码")
        conn.sendline(password)
        it.expect(conn, "登录尝试过于频繁")
        it.expect_not(conn, "3. 开始游戏", window=1.0)

        text = conn.raw.decode("utf-8", "replace")
        waits = re.findall(r"请 (\d+) 秒后再试", text)
        if not waits:
            raise it.Failure("[%s] 拒绝提示里没有等待秒数；最近输出：%r"
                             % (it.STAGE, conn.tail()))
        wait_seconds = int(waits[-1])
        window_seconds = max(1, args.window_ms // 1000)
        if wait_seconds > window_seconds + 1:
            raise it.Failure("[%s] 等待 %d 秒超过窗口长度 %d 秒"
                             % (it.STAGE, wait_seconds, window_seconds))

        # 同一个账号、同一份密码，换一个源地址就能进：限流是按 IP 的
        it.STAGE = "另一个源地址 %s 不受影响" % args.other_source_ip
        other = it.Conn(args.host, args.port, args.timeout,
                        source_ip=args.other_source_ip)
        try:
            it.expect(other, "1. 注册帐号")
            other.sendline("2")
            it.expect(other, "请输入用户名")
            other.sendline(user)
            it.expect(other, "请输入密码")
            other.sendline(password)
            it.expect(other, "3. 开始游戏", timeout=args.timeout)
        finally:
            other.close()

        it.STAGE = "等待 %d 秒后同一 IP 应恢复登录" % wait_seconds
        time.sleep(wait_seconds + 0.5)
        conn.sendline(password)
        it.expect(conn, "3. 开始游戏", timeout=args.timeout)
    except it.Failure as exc:
        it.annotate("error", "IP 兜底测试失败: %s" % exc)
        print("FAIL IP 兜底: %s" % exc, flush=True)
        return 1
    except OSError as exc:
        detail = "[%s] 网络错误: %s" % (it.STAGE, exc)
        it.annotate("error", "IP 兜底测试失败: %s" % detail)
        print("FAIL IP 兜底: %s" % detail, flush=True)
        return 1
    finally:
        conn.close()

    print("PASS IP 兜底（%.1fs）" % (time.time() - started), flush=True)
    it.annotate("notice", "IP 兜底协议测试通过：换用户名喷洒在 %d 次失败后被按 IP 拦截，"
                          "%s 同时可正常登录，窗口滑过后恢复"
                % (args.max_failures, args.other_source_ip))
    return 0


if __name__ == "__main__":
    sys.exit(main())
