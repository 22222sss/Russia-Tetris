#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tetris-bridge.py 的自动化测试（**不需要** SSH、也不需要真服务端）。

桥是 SSH 承载里唯一的新代码，它替终端做三件事：raw 模式、CRLF 翻译、本地回显。
这三件任意一件做错，玩家看到的现象都跟"telnet 不能用"那个老坑一样（方向键要回车
才生效 / 输入看不见 / 满屏乱码），而且**很难从截图上分辨是哪一件错了** —— 所以
这里逐条断言，而不是跑一遍看"能不能玩"。

覆盖：
1. 双向字节透传（含中文与 ANSI），stdout/stderr 不掺任何日志；
2. 回车翻译：raw 终端只发 CR，服务端要 CRLF 分帧；
3. 本地回显：--echo on 时回显、ESC 序列不回显；
4. stdin EOF -> 半关闭 socket（服务端收不到 EOF 就永远不释放 User 对象）；
5. 服务端先关闭 -> 桥自己退出（不会把 SSH 会话挂住）；
6. 连不上服务端 -> 退出码 1 + 屏幕上有可读提示（而不是卡死）；
7. 来源地址映射：同一客户端恒定、不同客户端不同（服务端"按 IP 限流"靠它才不退化成全站共享）；
8. （仅 Linux）真 pty：桥确实把终端切到 raw，方向键不按回车就到达服务端；
   顺带把桥进程的实测 RSS 打出来（部署文档里的每会话内存开销以此为准）；
9. （--server 时）穿过桥跑一遍**真协议**：注册 -> 登录 -> 主菜单 -> 进游戏 -> 按键。
   前 8 条只证明字节搬运对，这条才证明玩家真能玩。

用法：
    python3 tests/bridge_test.py                        # 只跑桥自身（不依赖服务端）
    python3 tests/bridge_test.py --server 127.0.0.1:9999 --db tetris.db
                                                        # 再加一条穿过桥的真协议用例

本地先对 mock 跑通再推 CI（CI 一轮好几分钟，日志还读不到）：

    python3 tests/mock_server.py 19999 /tmp/mock.db &
    python3 tests/bridge_test.py --server 127.0.0.1:19999 --db /tmp/mock.db
"""

import argparse
import os
import random
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE = os.path.normpath(os.path.join(HERE, "..", "deploy", "tetris-bridge.py"))

try:
    import pty
    import termios
    HAVE_PTY = sys.platform.startswith("linux")
except ImportError:
    HAVE_PTY = False

IS_LINUX = sys.platform.startswith("linux")


class Failure(Exception):
    """断言失败。"""


def annotate(level, message):
    msg = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::%s::%s" % (level, msg), flush=True)


# ---------------------------------------------------------------------------
# 测试用的最小服务端：只接受一条连接，记录收到的字节和对端地址
# ---------------------------------------------------------------------------

class RecorderServer:
    """127.0.0.1 上的单连接服务端。

    greeting：连上就发的字节；之后按 wait_eof 决定是读对端直到 EOF，还是立刻关闭。
    tail：读到 EOF（或直接关闭前）再发一段，用来验证"服务端最后那屏不会被丢掉"。
    """

    def __init__(self, greeting=b"", tail=b"", close_immediately=False):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.greeting = greeting
        self.tail = tail
        self.close_immediately = close_immediately
        self.peer = None
        self.received = bytearray()
        self.saw_eof = False
        self.error = None
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def _serve(self):
        try:
            conn, peer = self.sock.accept()
        except OSError as exc:
            self.error = exc
            return
        self.peer = peer[0]
        try:
            conn.settimeout(10.0)
            if self.greeting:
                conn.sendall(self.greeting)
            if not self.close_immediately:
                while True:
                    chunk = conn.recv(65536)
                    if not chunk:
                        self.saw_eof = True
                        break
                    self.received += chunk
                if self.tail:
                    conn.sendall(self.tail)
        except OSError as exc:
            self.error = exc
        finally:
            try:
                conn.close()
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass

    def wait(self, timeout=10.0):
        self.thread.join(timeout)
        return not self.thread.is_alive()


def bridge_env(extra=None):
    """干净的环境：把可能影响桥的变量和 SSH_CLIENT 都摘掉，测试才可重复。"""
    env = dict(os.environ)
    for name in list(env):
        if name.startswith("TETRIS_BRIDGE_"):
            env.pop(name, None)
    env.pop("SSH_CLIENT", None)
    env.pop("SSH_CONNECTION", None)
    if extra:
        env.update(extra)
    return env


def bridge_command(port, args=()):
    return [sys.executable, BRIDGE, "--target", "127.0.0.1:%d" % port] + list(args)


def run_bridge(port, args=(), stdin_data=b"", env=None, timeout=20.0):
    """跑一次桥，把 stdin 写完就关（顺带覆盖 stdin EOF 的路径）。"""
    proc = subprocess.Popen(bridge_command(port, args),
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            env=bridge_env(env))
    try:
        out, err = proc.communicate(stdin_data, timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
        raise Failure("桥没有在 %ss 内退出（stdin 已关闭），输出：%r" % (timeout, out))
    return proc.returncode, out, err


# ---------------------------------------------------------------------------
# 用例
# ---------------------------------------------------------------------------

GREETING = b"\x1b[2J\x1b[H\x1b[12;14H\x1b[37m" + "请选择操作：".encode("utf-8")


def test_passthrough_and_crlf():
    """双向透传 + CR->CRLF + stdout 纯净（日志不能出现在屏幕上）。"""
    server = RecorderServer(greeting=GREETING, tail=b"[tail]").start()
    log_path = tempfile.mktemp(prefix="bridge-log-")

    # 2 + 回车 + 方向键下 + 中文（UTF-8 多字节不能被截断/改写）
    typed = b"2\r\x1b[B" + "中文".encode("utf-8")
    rc, out, err = run_bridge(server.port, args=("--log", log_path), stdin_data=typed)
    server.wait()

    if rc != 0:
        raise Failure("桥退出码 %d（期望 0）；stdout=%r" % (rc, out))
    if out != GREETING + b"[tail]":
        raise Failure("stdout 内容不对：期望服务端字节原样，实际 %r" % out)
    if err:
        raise Failure("stderr 不该有任何输出（会写进玩家屏幕）：%r" % err)
    if bytes(server.received) != b"2\r\n\x1b[B" + "中文".encode("utf-8"):
        raise Failure("服务端收到的字节不对：%r" % bytes(server.received))
    if not server.saw_eof:
        raise Failure("stdin 关闭后服务端没读到 EOF（半关闭没做，User 对象不会被释放）")
    if os.path.exists(log_path):
        with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        os.unlink(log_path)
        if "会话开始" not in text:
            raise Failure("--log 里没有会话记录：%r" % text[:300])
    else:
        raise Failure("--log 指定的文件没有被创建")


def test_echo_on():
    """--echo on：可打印字符回显（否则登录时看不见自己输入）。"""
    server = RecorderServer().start()
    rc, out, err = run_bridge(server.port, args=("--echo", "on"), stdin_data=b"ab\r")
    server.wait()

    if rc != 0:
        raise Failure("桥退出码 %d（期望 0）" % rc)
    if out != b"ab\r\n":
        raise Failure("回显内容不对：期望 b'ab\\r\\n'，实际 %r" % out)
    if bytes(server.received) != b"ab\r\n":
        raise Failure("服务端收到的字节不对：%r" % bytes(server.received))


def test_escape_not_echoed():
    """方向键（ESC 序列）绝不回显，否则整屏会被转义序列弄乱。"""
    server = RecorderServer().start()
    rc, out, _ = run_bridge(server.port, args=("--echo", "on"), stdin_data=b"\x1b[B\x1b[D")
    server.wait()

    if rc != 0:
        raise Failure("桥退出码 %d（期望 0）" % rc)
    if out:
        raise Failure("ESC 序列被回显了：%r" % out)
    if bytes(server.received) != b"\x1b[B\x1b[D":
        raise Failure("方向键没有被原样转发：%r" % bytes(server.received))


def test_server_close_exits():
    """服务端先关闭时桥必须自己退出（否则 SSH 会话会挂在那里）。"""
    server = RecorderServer(greeting=b"bye", close_immediately=True).start()
    proc = subprocess.Popen(bridge_command(server.port),
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            env=bridge_env())
    try:
        rc = proc.wait(timeout=10)      # stdin 一直开着，桥仍然应该退出
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise Failure("服务端断开后桥没有退出（stdin 仍然开着）")
    out = proc.stdout.read()
    proc.stdin.close()
    proc.stdout.close()
    proc.stderr.close()
    server.wait()

    if rc != 0:
        raise Failure("服务端正常断开时退出码应为 0，实际 %d" % rc)
    if out != b"bye":
        raise Failure("断开前那一屏丢了：%r" % out)


def test_connect_failure():
    """连不上服务端：退出码 1 + 屏幕上有一句人话，而不是干等。"""
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    dead_port = probe.getsockname()[1]
    probe.close()                       # 端口空出来但没人监听

    rc, out, err = run_bridge(dead_port, stdin_data=b"")
    if rc != 1:
        raise Failure("退出码应为 1，实际 %d" % rc)
    text = out.decode("utf-8", "replace")
    if "游戏服务暂时不可用" not in text:
        raise Failure("屏幕上没有可读提示：%r" % text)
    if err:
        raise Failure("stderr 有输出（会写进玩家屏幕）：%r" % err)


def test_target_parse_error():
    """--target 写错时给可读提示，别抛栈。"""
    proc = subprocess.run([sys.executable, BRIDGE, "--target", "no-port"],
                          stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,
                          env=bridge_env(), timeout=20)
    if proc.returncode != 2:
        raise Failure("退出码应为 2，实际 %d" % proc.returncode)
    if "参数错误" not in proc.stdout.decode("utf-8", "replace"):
        raise Failure("没有可读的参数错误提示：%r" % proc.stdout)


def peer_for(client_ip, args=()):
    """用指定的 SSH_CLIENT 跑一次桥，返回服务端看到的对端地址。"""
    server = RecorderServer(close_immediately=True).start()
    run_bridge(server.port, args=args, stdin_data=b"",
               env={"SSH_CLIENT": "%s 40000 22" % client_ip})
    server.wait()
    return server.peer


def test_source_address_mapping():
    """来源地址映射：同一玩家恒定、不同玩家不同；关掉时固定 127.0.0.1。"""
    same_a = peer_for("203.0.113.7")
    same_b = peer_for("203.0.113.7")
    other = peer_for("198.51.100.9")
    local = peer_for("127.0.0.1")
    disabled = peer_for("203.0.113.7", args=("--source-ip", "default"))
    no_client = peer_for("", args=())

    if disabled != "127.0.0.1":
        raise Failure("--source-ip default 时应固定 127.0.0.1，实际 %r" % disabled)
    if local != "127.0.0.1":
        raise Failure("本机客户端（127.0.0.1）应退回默认源，实际 %r" % local)

    expected = "127.0.113.7"            # 203.0.113.7 的后三字节 0.113.7
    if same_a == expected:
        if same_b != same_a:
            raise Failure("同一客户端两次映射不一致：%r vs %r" % (same_a, same_b))
        if other == same_a:
            raise Failure("不同客户端映射到了同一地址：%r" % other)
        if not re.match(r"^127\.255\.\d+\.\d+$", no_client):
            raise Failure("IPv6 客户端应映射到 127.255.0.0/16，实际 %r" % no_client)
    elif same_a == "127.0.0.1" and other == "127.0.0.1" and no_client == "127.0.0.1":
        # 有些平台不允许 bind 任意 127/8 地址，桥按设计退回默认源
        if IS_LINUX:
            raise Failure("Linux 上来源地址映射没生效（127/8 整段应是本地地址）")
        annotate("notice", "本平台不允许 bind 任意 127/8 地址，桥退回 127.0.0.1（限流退化为全局额度）")
    else:
        raise Failure("来源地址映射结果不自洽：same_a=%r same_b=%r other=%r ipv6=%r"
                      % (same_a, same_b, other, no_client))


def read_rss_mb(pid):
    """读 /proc/<pid>/status 的 VmRSS（MB）；读不到返回 None。"""
    try:
        with open("/proc/%d/status" % pid, "r", encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except (OSError, ValueError):
        return None
    return None


def test_pty_raw_mode():
    """真 pty：桥必须切 raw，方向键不按回车就到达服务端（telnet 那个坑的回归防护）。"""
    if not HAVE_PTY:
        return "skipped"

    server = RecorderServer(greeting=b"hello").start()
    master, slave = pty.openpty()
    # 必须从 **slave** 取设备名：`os.ttyname(master)` 拿到的是 /dev/ptmx，
    # 那是另一台 pty，查到的 termios 永远是默认值（CI 第一次就是这么假失败的）。
    slave_name = os.ttyname(slave)
    log_path = tempfile.mktemp(prefix="bridge-pty-")

    proc = subprocess.Popen(bridge_command(server.port, args=("--log", log_path)),
                            stdin=slave,
                            stdout=slave,
                            stderr=subprocess.DEVNULL,
                            env=bridge_env(),
                            start_new_session=True)
    os.close(slave)                     # 桥已经继承了这份 fd

    def diagnostics():
        detail = "桥进程%s" % ("已退出(%r)" % proc.poll() if proc.poll() is not None else "仍在运行")
        if os.path.exists(log_path):
            with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
                detail += "；桥日志：%r" % handle.read()[-300:]
        return detail

    slave_fd = os.open(slave_name, os.O_RDWR | os.O_NOCTTY)
    try:
        # 1) 桥要把 pty 切到 raw：ICANON（行缓冲）和 ECHO（内核回显）都得关掉。
        #    ECHO 留着的话玩家会看到"内核回显 + 桥回显"两份字符。
        deadline = time.time() + 15
        lflag = termios.tcgetattr(slave_fd)[3]
        while time.time() < deadline:
            lflag = termios.tcgetattr(slave_fd)[3]
            if not (lflag & termios.ICANON) and not (lflag & termios.ECHO):
                break
            time.sleep(0.05)
        else:
            raise Failure("桥没有把 pty 切到 raw（lflag=0x%x，ICANON/ECHO 仍在）；%s"
                          % (lflag, diagnostics()))

        # 2) 关键回归：方向键不按回车也必须立刻到达服务端。
        os.write(master, b"\x1b[B")
        deadline = time.time() + 10
        while time.time() < deadline and b"\x1b[B" not in server.received:
            time.sleep(0.05)
        if b"\x1b[B" not in server.received:
            raise Failure("方向键没有即时到达服务端（pty 还在行缓冲）；服务端收到 %r；%s"
                          % (bytes(server.received), diagnostics()))

        # 3) 回车要翻成 CRLF（raw 模式下终端只发一个 CR）。
        os.write(master, b"\r")
        deadline = time.time() + 10
        while time.time() < deadline and b"\r\n" not in bytes(server.received):
            time.sleep(0.05)
        if b"\r\n" not in bytes(server.received):
            raise Failure("回车没有被翻成 CRLF：%r" % bytes(server.received))

        # 4) 顺带量一下每会话的内存开销（部署文档要引用这个数）。
        rss = read_rss_mb(proc.pid)
        if rss is not None:
            annotate("notice", "桥进程实测 RSS: %.1f MB（每会话一个进程）" % rss)
    finally:
        os.close(slave_fd)
        os.close(master)                # 关掉 pty -> 桥读到 stdin EOF
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        server.wait()
        if os.path.exists(log_path):
            os.unlink(log_path)

    if proc.returncode not in (0, -15, -1):
        # 0 = 正常；-15/-1 = 关掉 pty 时被 SIGHUP/SIGTERM 收走（sshd 断连也是这个路径）
        raise Failure("关掉 pty 之后桥的退出码异常：%r" % proc.returncode)
    return "ok"


# ---------------------------------------------------------------------------
# 端到端：穿过桥跑一遍真协议（--server 才跑，需要真服务端或 tests/mock_server.py）
# ---------------------------------------------------------------------------

class BridgeSession:
    """一条"SSH 会话"：桥的 stdin/stdout 就是本进程的管道。

    和 integration_test.py 的 `Conn` 一样用 **mark**：`wait_for` 只看上次发送之后
    新到达的输出。不这么做的话，后面那句 `expect("2. 登录")` 会直接命中开头的初始
    菜单 —— 于是把两条命令一次发出去，而服务端的 `ReceiveData` 一次 recv 只取
    `\\r\\n` 之前那段、把后面的丢掉（见 6.10 那类多行合并坑）。
    """

    def __init__(self, target, args=(), env=None):
        self.proc = subprocess.Popen(
            [sys.executable, BRIDGE, "--target", target] + list(args),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=bridge_env(env))
        self.buffer = bytearray()
        self.lock = threading.Lock()
        self.mark = 0
        self.eof = False
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            # read1：有多少读多少。用 read(n) 会一直等到攒满 n 字节，
            # 小帧（菜单/按键响应）就永远读不出来。
            chunk = self.proc.stdout.read1(4096)
            if not chunk:
                break
            with self.lock:
                self.buffer += chunk
        with self.lock:
            self.eof = True

    def text(self):
        with self.lock:
            return bytes(self.buffer).decode("utf-8", "replace")

    def tail(self, limit=400):
        return self.text()[-limit:]

    def _find(self, needle_bytes):
        with self.lock:
            return bytes(self.buffer).find(needle_bytes, self.mark)

    def wait_for(self, needle, timeout=20.0):
        """等 mark 之后出现 needle；命中后把 mark 推到它末尾。"""
        needle_bytes = needle.encode("utf-8")
        deadline = time.time() + timeout
        while True:
            index = self._find(needle_bytes)
            if index >= 0:
                with self.lock:
                    self.mark = index + len(needle_bytes)
                return True
            if time.time() > deadline:
                return False
            if self.eof:
                return False
            time.sleep(0.05)

    def expect(self, needle, timeout=20.0):
        if not self.wait_for(needle, timeout):
            raise Failure("等不到 %r；桥的输出：%r" % (needle, self.tail()))

    def send(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        with self.lock:
            self.mark = len(self.buffer)     # 之后的断言只看新输出
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def sendline(self, line):
        self.send(line + "\r\n")

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        try:
            self.proc.stdout.close()
            self.proc.stderr.close()
        except OSError:
            pass


def test_protocol_through_bridge(target, db_path):
    """穿过桥走主链路：注册 -> 登录 -> 主菜单 -> 选难度 -> 进游戏 -> 按键。

    这条是"桥 + 服务端"的合练：上一条用例证明字节搬运对，这条证明玩家真能玩
    （菜单按 CRLF 分帧、登录要等异步哈希、游戏里的方向键是裸 ESC 序列）。
    顺便让桥带上假 SSH_CLIENT，走一遍真实的"来源地址映射 -> 服务端 accept"路径。
    """
    user = "br%d%d" % (int(time.time()) % 100000, random.randint(100, 999))
    password = "pw" + str(random.randint(100000, 999999))

    session = BridgeSession(target, env={"SSH_CLIENT": "203.0.113.9 40000 22"})
    try:
        session.expect("1. 注册帐号")
        session.sendline("1")
        session.expect("请输入用户名")
        session.sendline(user)
        session.expect("请输入密码")
        session.sendline(password)
        session.expect("注册成功")

        if db_path:
            import db                      # 只依赖标准库；与其它测试脚本共用
            if not db.wait_for_account(db_path, user, 20.0):
                raise Failure("账号 %r 没有落库（注册走异步写线程，库=%s）"
                              % (user, db_path))

        session.sendline("3")
        session.expect("2. 登录")
        session.sendline("2")
        session.expect("请输入用户名")
        session.sendline(user)
        session.expect("请输入密码")
        session.sendline(password)
        session.expect("3. 开始游戏", timeout=30)     # 登录要跑一次 PBKDF2

        session.sendline("3")
        session.expect("1. 简单模式")
        session.sendline("1")
        session.expect("Next:")
        session.expect("Score:")

        # 游戏里方向键是裸 ESC 序列（不带 CRLF），桥必须原样转发
        session.send(b"\x1b[B")
        time.sleep(0.3)
        session.send(b"\x1b[D")
        time.sleep(0.3)
        if session.proc.poll() is not None:
            raise Failure("按键之后桥退出了（服务端断开了连接？）")
        if session.eof:
            raise Failure("按键之后会话被服务端关闭")
    finally:
        session.close()
    return "ok"


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

UNIT_CHECKS = [
    ("字节透传 + 回车翻译 + stdout 纯净", test_passthrough_and_crlf),
    ("本地回显", test_echo_on),
    ("ESC 序列不回显", test_escape_not_echoed),
    ("服务端关闭后桥自行退出", test_server_close_exits),
    ("连不上服务端时的提示与退出码", test_connect_failure),
    ("--target 参数错误提示", test_target_parse_error),
    ("来源地址映射", test_source_address_mapping),
    ("pty raw 模式与方向键即时转发", test_pty_raw_mode),
]


def main():
    parser = argparse.ArgumentParser(description="tetris-bridge 的自动化测试")
    parser.add_argument("--server", default="",
                        help="可选：服务端 host:port，给了就再加一条「穿过桥的真协议」用例")
    parser.add_argument("--db", default="",
                        help="配合 --server：服务端的 SQLite 库路径（验证注册确实落库）")
    parser.add_argument("--skip-unit", action="store_true",
                        help="只跑端到端用例（CI 里桥自身用例已经单独跑过一遍）")
    args = parser.parse_args()

    if not os.path.exists(BRIDGE):
        annotate("error", "找不到 %s" % BRIDGE)
        return 1

    checks = [] if args.skip_unit else list(UNIT_CHECKS)
    if args.server:
        checks.append(("穿过桥跑真协议（%s）" % args.server,
                       lambda: test_protocol_through_bridge(args.server, args.db)))
    if not checks:
        parser.error("没有要跑的用例（--skip-unit 需要配合 --server）")

    print("桥程序: %s" % BRIDGE, flush=True)
    failed = 0
    for name, fn in checks:
        started = time.time()
        try:
            result = fn()
        except Failure as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "桥测试失败 - %s: %s" % (name, exc))
        except (OSError, subprocess.SubprocessError) as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "桥测试失败 - %s: %s" % (name, exc))
        else:
            suffix = "" if result != "skipped" else "（跳过：需要 Linux pty）"
            print("PASS %s (%.1fs)%s" % (name, time.time() - started, suffix), flush=True)

    if failed:
        print("桥测试失败: %d/%d 项" % (failed, len(checks)), flush=True)
        return 1

    print("桥测试全部通过: %d/%d 项" % (len(checks), len(checks)), flush=True)
    annotate("notice", "桥测试全部通过（%d 项）" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
