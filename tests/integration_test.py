#!/usr/bin/env python3
"""协议级集成测试：注册 -> 返回菜单 -> 登录 -> 排行榜 -> 开始游戏 -> 按键 -> 游戏结束。

只依赖 Python 标准库（CI 的 ubuntu runner 自带 python3，无需安装额外包）。

设计要点
--------
1. 服务端是"渲染终端"协议：它下发 ANSI 转义序列 + 文本片段，因此断言前先剥离
   转义序列，只匹配稳定的文本片段（如 "请输入用户名"、"注册成功"），
   不比对整屏内容，避免因布局微调而误报。
2. 菜单类输入一律以 CRLF 结尾（Server::ReceiveData 用 "\\r\\n" 分帧）；
   进入游戏后是原始按键（服务端用 strcmp(buffer, "\\x1b[B") 判断），不能带 CRLF。
3. 注册后的落盘和登录后的菜单渲染都是**异步**的（saveNewUserDataAsync /
   loadPlayerDataAsync），所以这里用轮询等待，不假设时序。
4. 失败信息用 ::error:: 注解输出：GitHub Actions 的日志下载需要 token，
   只有 annotations 能读回来（见 docs/HANDOVER.md 第 6 节第 7 条）。

用法
----
    python3 tests/integration_test.py --host 127.0.0.1 --port 9999

服务端的工作目录决定 userdata.csv 的位置，本脚本的 --userdata 默认相对当前目录，
所以请在服务端同一个目录下运行（CI 里两者都是 src/）。

改了这个脚本本身后，可以先用 tests/mock_server.py 在本地把它跑通，不必等 CI：

    python3 tests/mock_server.py 19999 /tmp/mock_userdata.csv &
    python3 tests/integration_test.py --port 19999 --userdata /tmp/mock_userdata.csv
"""

import argparse
import random
import re
import socket
import sys
import time

# ANSI 转义：CSI（颜色/光标/清屏）、OSC、以及 ESC + 单字符
ANSI_RE = re.compile(
    rb"\x1b\[[0-9;?]*[ -/]*[@-~]"      # CSI ... 终止符
    rb"|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)"  # OSC ... BEL / ST
    rb"|\x1b[()#][0-9A-Za-z]"          # 字符集选择
    rb"|\x1b[@-_]"                     # 其余两字节序列
)


class Failure(Exception):
    """断言失败。"""


def annotate(level, message):
    """输出 GitHub Actions 注解（单行，需转义 % \\r \\n）。"""
    msg = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::%s::%s" % (level, msg), flush=True)


class Conn:
    """一条到服务端的连接，负责累积输出并等待文本片段出现。"""

    def __init__(self, host, port, timeout=15.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(0.2)
        self.raw = bytearray()
        self.mark = 0
        self.timeout = timeout

    # --- 输出处理 ---

    def tail(self, limit=500):
        """最近输出的可读摘要，用于失败诊断（只处理尾部，避免整段缓冲区做正则）。"""
        text = ANSI_RE.sub(b"", bytes(self.raw[-8192:])).decode("utf-8", "replace")
        return " ".join(text.split())[-limit:]

    def mark_now(self):
        """把"当前已收到的输出"划为旧输出，之后只看新输出。"""
        self.mark = len(self.raw)

    # --- 发送 ---

    def send(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.mark_now()
        self.sock.sendall(data)

    def sendline(self, line):
        """菜单类输入：以 CRLF 结束一帧。"""
        self.send(line + "\r\n")

    # --- 接收 ---

    def wait_for(self, needle, timeout=None):
        """等待 needle 出现在 mark 之后的新输出中。

        直接在原始字节流里查找：服务端每段文字都是"定位+颜色+文本"一次 send
        发出来的，文本在流里是连续的，不会被转义序列切断；同时省掉了每次
        断言都对整段缓冲区跑正则的开销（游戏中红屏会迅速滚到几百 KB）。
        """
        needle_bytes = needle.encode("utf-8")
        limit = time.time() + (self.timeout if timeout is None else timeout)
        while True:
            # 只把 mark 推到"匹配串末尾"：一次 recv 常带回多段文本
            # （如 "Next:" 和 "Score:"），推到末尾会让下一次断言看不到同批内容。
            index = self.raw.find(needle_bytes, self.mark)
            if index >= 0:
                self.mark = index + len(needle_bytes)
                return True
            if time.time() > limit:
                return False
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return False
            if not chunk:          # 服务端关闭了连接
                return False
            self.raw += chunk

    def contains(self, needle):
        """mark 之后是否已出现 needle（不推进 mark，供轮询用）。"""
        return self.raw.find(needle.encode("utf-8"), self.mark) >= 0

    def drain(self, timeout=0.001):
        """把当前可读的数据都读进来，不推进 mark。"""
        previous = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            while True:
                try:
                    chunk = self.sock.recv(65536)
                except (socket.timeout, BlockingIOError):
                    return
                except OSError:
                    return
                if not chunk:
                    return
                self.raw += chunk
        finally:
            self.sock.settimeout(previous)

    def alive(self, window=1.0):
        """在 window 秒内持续读，若始终没有 EOF 则认为连接仍存活。"""
        limit = time.time() + window
        while time.time() < limit:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return True
            except OSError:
                return False
            if not chunk:
                return False
            self.raw += chunk
        return True

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


STAGE = "初始化"


def expect(conn, needle, timeout=None):
    """等待某个文本片段出现；失败时带上阶段名和最近输出。"""
    if conn.wait_for(needle, timeout):
        return
    raise Failure("[%s] 未等到 %r；最近输出：%r" % (STAGE, needle, conn.tail()))


def expect_not(conn, needle, window=1.5):
    """在 window 秒内不应出现某个文本片段（用于"不应发生"的断言）。"""
    if conn.wait_for(needle, window):
        raise Failure("[%s] 不应出现 %r；最近输出：%r" % (STAGE, needle, conn.tail()))


# --------------------------------------------------------------------------
# 各测试阶段
# --------------------------------------------------------------------------

def new_account_name():
    """每次运行用唯一账号，避免与既有 userdata.csv 冲突。"""
    return "it%d%d" % (int(time.time()) % 100000, random.randint(100, 999))


def wait_for_userdata(path, user, timeout):
    """轮询 userdata.csv 直到出现账号名。

    注册走的是 saveNewUserDataAsync（异步写盘），登录校验却同步读文件，
    所以这里必须等落盘，否则立刻登录会偶发 "登录失败"。
    """
    limit = time.time() + timeout
    while time.time() < limit:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                if user in fh.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.2)
    return False


def test_register_and_login(args, user, password):
    """主链路：注册 -> 回主菜单 -> 登录 -> 主菜单 -> 选难度 -> 进入游戏。"""
    global STAGE

    STAGE = "连接并显示初始菜单"
    conn = Conn(args.host, args.port, args.timeout)
    try:
        expect(conn, "1. 注册帐号")

        STAGE = "选择注册"
        conn.sendline("1")
        expect(conn, "请输入用户名")

        STAGE = "输入注册用户名"
        conn.sendline(user)
        expect(conn, "请输入密码")

        STAGE = "输入注册密码"
        conn.sendline(password)
        expect(conn, "注册成功")

        # 注册是异步写盘的，而登录校验同步读文件 —— 必须等落盘再登录
        STAGE = "等待注册结果落盘（异步保存）"
        if not wait_for_userdata(args.userdata, user, args.timeout):
            raise Failure("[%s] 在 %s 中未找到 %r（超时 %ss）"
                          % (STAGE, args.userdata, user, args.timeout))

        STAGE = "注册后按 3 返回主菜单"
        conn.sendline("3")
        expect(conn, "2. 登录")

        STAGE = "选择登录"
        conn.sendline("2")
        expect(conn, "请输入用户名")

        STAGE = "输入登录用户名"
        conn.sendline(user)
        expect(conn, "请输入密码")

        STAGE = "输入登录密码"
        conn.sendline(password)
        # 登录菜单由异步线程 loadPlayerDataAsync 的回调渲染，需等待
        expect(conn, "3. 开始游戏", timeout=args.timeout)

        # ---- 排行榜：最近成绩（新账号应为空） ----
        STAGE = "查看最近成绩（新账号）"
        conn.sendline("1")
        expect(conn, "暂无成绩记录")
        # 无成绩时异步回调直接 return，不会打印"请按3返回菜单"；直接发 3 回主菜单
        conn.sendline("3")
        expect(conn, "3. 开始游戏")

        # ---- 排行榜：全服 top 成绩 ----
        STAGE = "查看全服排行榜"
        conn.sendline("2")
        expect(conn, "简单模式：")
        expect(conn, "用户名 分数")
        expect(conn, "请按3返回菜单")
        conn.sendline("3")
        expect(conn, "3. 开始游戏")

        STAGE = "进入难度选择"
        conn.sendline("3")
        expect(conn, "1. 简单模式")

        STAGE = "选择简单模式并进入游戏"
        conn.sendline("1")
        # 注意顺序：InitInterface 先画 "Next:"（第 2 行）再画 "Score:"（第 14 行）。
        # wait_for 只匹配 mark 之后的新输出，所以必须先等先出现的那个。
        expect(conn, "Next:")
        expect(conn, "Score:")

        STAGE = "游戏中发送方向键"
        for key in (b"\x1b[B", b"\x1b[D", b"\x1b[C"):
            conn.sock.sendall(key)
            time.sleep(0.15)

        STAGE = "按键后连接应仍然存活"
        if not conn.alive(1.0):
            raise Failure("按下方向键后服务端关闭了连接；最近输出：%r" % conn.tail())
    finally:
        conn.close()


def test_userdata_written(args, user):
    """注册应异步落盘到 userdata.csv（在服务端工作目录）。"""
    global STAGE
    STAGE = "userdata.csv 落盘"

    if not wait_for_userdata(args.userdata, user, args.timeout):
        raise Failure("在 %s 中未找到新注册的账号 %r（超时 %ss）"
                      % (args.userdata, user, args.timeout))


def test_login_again(args, user, password):
    """换一条连接重新登录，验证账号确实持久化、可再次认证。"""
    global STAGE

    STAGE = "二次登录：连接"
    conn = Conn(args.host, args.port, args.timeout)
    try:
        expect(conn, "1. 注册帐号")

        STAGE = "二次登录：选择登录"
        conn.sendline("2")
        expect(conn, "请输入用户名")

        STAGE = "二次登录：输入用户名"
        conn.sendline(user)
        expect(conn, "请输入密码")

        STAGE = "二次登录：输入密码"
        conn.sendline(password)
        expect(conn, "3. 开始游戏", timeout=args.timeout)
    finally:
        conn.close()


def test_wrong_password(args, user):
    """错误密码必须被拒绝，且不能进到主菜单。"""
    global STAGE

    STAGE = "错误密码：连接"
    conn = Conn(args.host, args.port, args.timeout)
    try:
        expect(conn, "1. 注册帐号")

        STAGE = "错误密码：选择登录"
        conn.sendline("2")
        expect(conn, "请输入用户名")

        STAGE = "错误密码：输入用户名"
        conn.sendline(user)
        expect(conn, "请输入密码")

        STAGE = "错误密码：输入错误密码"
        conn.sendline("definitely-not-the-password-" + str(random.randint(1000, 9999)))
        expect(conn, "登录失败")

        STAGE = "错误密码：不应进入主菜单"
        expect_not(conn, "3. 开始游戏", window=1.5)
    finally:
        conn.close()


def test_fast_reconnect(args):
    """快速重连：上一条连接刚关闭时，新连接通常复用同一个 fd。

    fd 复用撞上"死连接还没被 100ms 清理定时器移除"的窗口时，
    旧实现（users.emplace）会插入失败并留下僵尸映射，新连接对输入无响应。
    这个用例是该 bug 的回归防护。
    """
    global STAGE

    STAGE = "快速重连：第一条连接"
    first = Conn(args.host, args.port, args.timeout)
    try:
        expect(first, "1. 注册帐号")
    finally:
        first.close()   # 立刻关闭，制造 fd 复用窗口

    STAGE = "快速重连：立刻建立第二条连接"
    second = Conn(args.host, args.port, args.timeout)
    try:
        expect(second, "1. 注册帐号")

        STAGE = "快速重连：新连接必须能响应输入"
        second.sendline("1")
        expect(second, "请输入用户名")
    finally:
        second.close()


def test_game_over(args, user, password):
    """游戏结束流程：困难模式把棋盘堆满 -> GAME OVER -> 按 n 回主菜单。

    不校验分数是否落盘：这里没有消行、分数为 0，未必会写入成绩记录，
    那样断言会变成"假失败"。该用例覆盖的是结束状态机与"重新开始/退出"分支。
    """
    global STAGE

    STAGE = "游戏结束：登录"
    conn = Conn(args.host, args.port, args.timeout)
    try:
        expect(conn, "1. 注册帐号")
        conn.sendline("2")
        expect(conn, "请输入用户名")
        conn.sendline(user)
        expect(conn, "请输入密码")
        conn.sendline(password)
        expect(conn, "3. 开始游戏")

        STAGE = "游戏结束：选择困难模式"
        conn.sendline("3")
        expect(conn, "3. 困难模式")
        conn.sendline("3")
        expect(conn, "Next:")

        STAGE = "游戏结束：狂按下键把棋盘堆满"
        # 困难模式 0.2s 落一格。服务端用 strcmp 比对整段 recv，
        # 合并发送的多个按键会被整段丢弃，所以必须逐次发送。
        deadline = time.time() + args.game_over_timeout
        while time.time() < deadline and not conn.contains("GAME OVER"):
            conn.sock.sendall(b"\x1b[B")
            time.sleep(0.04)
            conn.drain()

        STAGE = "游戏结束：等待结束提示"
        expect(conn, "GAME OVER", timeout=10)
        expect(conn, "Start Again ? (y/n):")

        STAGE = "游戏结束：按 n 返回主菜单"
        conn.send("n")
        expect(conn, "1. 注册帐号", timeout=10)
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description="俄罗斯方块服务端协议级集成测试")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--userdata", default="userdata.csv",
                        help="userdata.csv 路径（相对服务端工作目录）")
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="单个断言的等待上限（秒）")
    parser.add_argument("--game-over-timeout", type=float, default=45.0,
                        help="把棋盘堆满以触发 GAME OVER 的等待上限（秒）")
    args = parser.parse_args()

    user = new_account_name()
    password = "pw" + str(random.randint(100000, 999999))
    print("测试账号: %s" % user, flush=True)

    checks = [
        ("注册/登录/进入游戏", lambda: test_register_and_login(args, user, password)),
        ("account 落盘", lambda: test_userdata_written(args, user)),
        ("二次登录", lambda: test_login_again(args, user, password)),
        ("错误密码被拒绝", lambda: test_wrong_password(args, user)),
        ("快速重连后仍能响应输入", lambda: test_fast_reconnect(args)),
        ("游戏结束流程", lambda: test_game_over(args, user, password)),
    ]

    failed = 0
    for name, fn in checks:
        started = time.time()
        try:
            fn()
        except Failure as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "集成测试失败 - %s: %s" % (name, exc))
        except OSError as exc:
            failed += 1
            msg = "[%s] 网络错误: %s" % (STAGE, exc)
            print("FAIL %s: %s" % (name, msg), flush=True)
            annotate("error", "集成测试失败 - %s: %s" % (name, msg))
        else:
            print("PASS %s (%.1fs)" % (name, time.time() - started), flush=True)

    if failed:
        print("集成测试失败: %d/%d 项" % (failed, len(checks)), flush=True)
        return 1

    print("集成测试全部通过: %d/%d 项" % (len(checks), len(checks)), flush=True)
    annotate("notice", "集成测试全部通过（%d 项）" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
