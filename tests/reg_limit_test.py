#!/usr/bin/env python3
"""注册限流的协议级测试（需要一个**低阈值**启动的服务端）。

为什么不能只靠 C++ 自检：`tests/reg_throttle_test.cpp` 只验证状态机。
"注册的 IP 真的取自 accept 的源地址""拒绝发生在 PBKDF2（写线程）之前"
"拦的是 IP 额度 / 全局总量，而不是某个用户名"——这三件事只有走真实协议才能证明。

用法（在服务端的工作目录下执行，SQLite 库就在那儿）：
    TETRIS_REG_IP_MAX=3 TETRIS_REG_IP_WINDOW_MS=10000 \\
    TETRIS_REG_GLOBAL_MAX=6 TETRIS_REG_GLOBAL_WINDOW_MS=10000 \\
    TETRIS_PORT=10016 ./tetris_game &
    python3 ../tests/reg_limit_test.py --port 10016 --db tetris.db \\
        --ip-max 3 --ip-window-ms 10000 --global-max 6 --global-window-ms 10000

断言：
1. 同一 IP 在额度内注册 N 个账号全部成功；
2. 第 N+1 个被拦：提示"注册过于频繁，请 M 秒后再试"，M 不超过窗口，按 3 能回上级菜单；
3. 换一个源地址还能注册 —— 证明是按 IP 隔离，不是"有人注册就全站封"；
4. 继续换 IP 直到全局额度用完，**下一个新 IP 也被拦** —— 证明全局那道也真的在起作用
   （参数要求：global-max > ip-max + 1）；
5. 窗口滑过后同一 IP 恢复 —— 证明不会永久锁死。
"""

import argparse
import random
import re
import sys
import time

import integration_test as it


def init_menu(conn):
    """确认回到了最初的选择菜单（1 注册 / 2 登录）。"""
    it.expect(conn, "1. 注册帐号")


def try_register(conn, user, password, timeout):
    """提交一次注册，返回 ("ok"|"throttled", 等待秒数)。

    调用前必须已经在初始菜单上（新连接要先 `init_menu(conn)`；同一连接上一次调用
    结束时已经按 3 回到初始菜单，所以循环里不用再等）。返回时同样停在初始菜单。
    """
    conn.sendline("1")
    it.expect(conn, "请输入用户名")
    conn.sendline(user)
    it.expect(conn, "请输入密码")
    conn.sendline(password)

    hit = it.wait_for_any(conn, ["注册成功！", "注册过于频繁"], timeout)
    if hit is None:
        raise it.Failure("[%s] 既没注册成功也没被限流；最近输出：%r"
                         % (it.STAGE, conn.tail()))
    it.expect(conn, "请按3返回上级菜单")
    wait_seconds = 0
    if hit == "注册过于频繁":
        text = conn.raw.decode("utf-8", "replace")
        waits = re.findall(r"请 (\d+) 秒后再试", text)
        if not waits:
            raise it.Failure("[%s] 限流提示里没有等待秒数；最近输出：%r"
                             % (it.STAGE, conn.tail()))
        wait_seconds = int(waits[-1])
    conn.sendline("3")
    init_menu(conn)
    return ("ok" if wait_seconds == 0 else "throttled"), wait_seconds


def make_account(tag):
    return it.new_account_name() + tag


def main():
    parser = argparse.ArgumentParser(description="注册限流的协议级测试")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=10016)
    parser.add_argument("--db", default="tetris.db")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--ip-max", type=int, default=3,
                        help="必须与服务端 TETRIS_REG_IP_MAX 一致")
    parser.add_argument("--ip-window-ms", type=int, default=10000,
                        help="必须与服务端 TETRIS_REG_IP_WINDOW_MS 一致")
    parser.add_argument("--global-max", type=int, default=6,
                        help="必须与服务端 TETRIS_REG_GLOBAL_MAX 一致")
    parser.add_argument("--global-window-ms", type=int, default=10000,
                        help="必须与服务端 TETRIS_REG_GLOBAL_WINDOW_MS 一致")
    args = parser.parse_args()

    if args.global_max <= args.ip_max + 1:
        print("参数不合理：要验证\"全局额度\"这一层，--global-max 必须 > --ip-max + 1"
              "（当前 ip-max=%d global-max=%d）" % (args.ip_max, args.global_max),
              file=sys.stderr)
        return 2

    password = "pw" + str(random.randint(100000, 999999))
    started = time.time()
    print("阈值：每 IP %d 次/%dms，全局 %d 次/%dms"
          % (args.ip_max, args.ip_window_ms, args.global_max, args.global_window_ms),
          flush=True)

    conn = it.Conn(args.host, args.port, args.timeout, source_ip="127.0.0.1")
    try:
        init_menu(conn)    # 新连接的第一屏是初始菜单

        # 1) 同一 IP 用满额度：全部成功
        it.STAGE = "同一 IP 用满 %d 次额度" % args.ip_max
        for i in range(args.ip_max):
            outcome, _ = try_register(conn, make_account("r%d" % i), password, args.timeout)
            if outcome != "ok":
                raise it.Failure("[%s] 第 %d 次注册就被限流了（额度没用满）"
                                 % (it.STAGE, i + 1))

        # 2) 第 N+1 次：拒绝，且等待时间不超过窗口
        it.STAGE = "超出单 IP 额度后应被拒"
        outcome, wait_seconds = try_register(conn, make_account("over"), password,
                                             args.timeout)
        if outcome != "throttled":
            raise it.Failure("[%s] 第 %d 次注册居然成功了（限流没生效）"
                             % (it.STAGE, args.ip_max + 1))
        window_seconds = max(1, args.ip_window_ms // 1000)
        if wait_seconds > window_seconds + 1:
            raise it.Failure("[%s] 等待 %d 秒超过窗口长度 %d 秒"
                             % (it.STAGE, wait_seconds, window_seconds))

        # 3) 换源地址：同一个服务端，另一个 IP 还能注册
        it.STAGE = "另一个源地址不受影响（按 IP 隔离）"
        other = it.Conn(args.host, args.port, args.timeout, source_ip="127.0.0.2")
        try:
            init_menu(other)
            outcome, _ = try_register(other, make_account("b"), password, args.timeout)
            if outcome != "ok":
                raise it.Failure("[%s] 127.0.0.2 也被限流了（不是按 IP 隔离？）" % it.STAGE)
        finally:
            other.close()

        # 4) 继续换 IP：把**全局**额度用完后，新 IP 也会被拦
        remaining = args.global_max - args.ip_max - 1
        it.STAGE = "换 IP 直到全局额度用完（还要 %d 次）" % remaining
        for i in range(remaining):
            source_ip = "127.0.0.%d" % (10 + i)   # 10.1、10.2… 都是独立源地址
            extra = it.Conn(args.host, args.port, args.timeout, source_ip=source_ip)
            try:
                init_menu(extra)
                outcome, _ = try_register(extra, make_account("g%d" % i), password,
                                          args.timeout)
                if outcome != "ok":
                    raise it.Failure("[%s] 全局额度还没用完就把 %s 拦了（只允许 %d 次）"
                                     % (it.STAGE, source_ip, args.global_max))
            finally:
                extra.close()

        it.STAGE = "全局额度用完后，全新的 IP 也应被拦"
        fresh = it.Conn(args.host, args.port, args.timeout, source_ip="127.0.0.99")
        try:
            init_menu(fresh)
            outcome, wait_seconds = try_register(fresh, make_account("blocked"), password,
                                                 args.timeout)
            if outcome != "throttled":
                raise it.Failure("[%s] 新 IP %s 居然还能注册（全局额度没生效）"
                                 % (it.STAGE, "127.0.0.99"))
            global_window_seconds = max(1, args.global_window_ms // 1000)
            if wait_seconds > global_window_seconds + 1:
                raise it.Failure("[%s] 等待 %d 秒超过全局窗口 %d 秒"
                                 % (it.STAGE, wait_seconds, global_window_seconds))

            # 5) 窗口滑过后恢复
            it.STAGE = "等 %d 秒后恢复注册" % wait_seconds
            time.sleep(wait_seconds + 0.5)
            outcome, _ = try_register(fresh, make_account("after"), password, args.timeout)
            if outcome != "ok":
                raise it.Failure("[%s] 窗口滑过后仍然被限流（永久锁死？）" % it.STAGE)
        finally:
            fresh.close()
    except it.Failure as exc:
        it.annotate("error", "注册限流测试失败: %s" % exc)
        print("FAIL 注册限流: %s" % exc, flush=True)
        return 1
    except OSError as exc:
        detail = "[%s] 网络错误: %s" % (it.STAGE, exc)
        it.annotate("error", "注册限流测试失败: %s" % detail)
        print("FAIL 注册限流: %s" % detail, flush=True)
        return 1
    finally:
        conn.close()

    print("PASS 注册限流（%.1fs）" % (time.time() - started), flush=True)
    it.annotate("notice", "注册限流协议测试通过：单 IP %d 次后被拦、换 IP 可继续、"
                          "全局 %d 次后新 IP 也被拦、窗口滑过后恢复"
                % (args.ip_max, args.global_max))
    return 0


if __name__ == "__main__":
    sys.exit(main())
