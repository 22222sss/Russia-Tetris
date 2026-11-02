#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tetris-bridge：把一条 SSH 会话（stdin/stdout）接到本机 tetris_game 的 TCP 端口。

为什么需要它
------------
服务端只讲 TCP（默认 0.0.0.0:9999，明文）。要让它"能上公网"，最省事的做法是
**不碰协议**：让 sshd 用 ForceCommand 把每个登录会话的 stdin/stdout 交给这个桥，
桥再去连本机的 9999。于是：

  * 传输加密由 SSH 提供 —— 明文密码不再过网，也不需要自己写 TLS；
  * 服务端一行代码都不用改（协议、状态机、限流、连接池全都照旧）；
  * 服务端可以只绑 127.0.0.1（TETRIS_BIND），公网唯一入口变成 sshd。

桥必须替终端做的事
------------------
这些是 SSH/telnet 与这个服务端之间的三个坑（`telnet` 为什么不能用见
docs/HANDOVER.md 6.1），必须由桥补上：

1. **raw 模式**：sshd 分配的是真 pty，默认 canonical + echo。canonical 模式下
   方向键会一直攒到回车才发出去，游戏就没法玩 —— 所以桥连上服务端后立刻把
   stdin 切到 raw，退出时恢复；
2. **回车翻译**：raw 模式下按回车只发一个 CR（0x0D），而服务端按 "\\r\\n"
   分帧（Server::ReceiveData）。桥把 CR 补成 CRLF；
3. **本地回显**：服务端不逐字符回显，raw 又关掉了 pty 的 echo。不补的话输入
   用户名时完全看不到自己在打什么。桥只回显可打印字符（含 UTF-8 多字节），
   ESC 序列（方向键）一律不回显，否则整屏会被转义序列弄乱。

**stdout 只能有游戏字节**：任何日志打到 stderr 或 stdout 都会写进玩家屏幕
（sshd 把 stderr 也接到同一个 pty）。所以诊断信息只走 syslog 或文件。

来源 IP 的保真（默认开，见 --source-ip）
----------------------------------------
服务端按 accept 的源地址做限流（IpThrottle：300 次/5 分钟）。所有会话都从
127.0.0.1 进来时，这个额度就退化成"全站共享一份"，正常玩家的输错会把所有人
一起锁住。桥用 SSH_CLIENT 里的真实客户端地址算出该客户端**专属**的 127/8 源
地址（IPv4 保留后三字节：a.b.c.d -> 127.b.c.d），于是：

  * 同一个玩家恒定同一地址，不同玩家互不吃额度；
  * 服务端日志里看到的是 127.b.c.d，不等于真实 IP（伪匿名），但按玩家可区分。

映射失败（目标不是本机回环、平台不允许 bind 127/8 等）时退回 127.0.0.1，
并在日志里记一行 —— 那时限流退化为全局额度，见 docs/SSH_DEPLOY.md。

用法
----
    sshd_config:  Match User tetris
                      ForceCommand /usr/local/bin/tetris-bridge

    tetris-bridge [--target 127.0.0.1:9999] [--connect-timeout 3]
                  [--source-ip hash|default] [--echo auto|on|off]
                  [--log /var/log/tetris-bridge.log]

环境变量（命令行优先）：TETRIS_BRIDGE_TARGET / _CONNECT_TIMEOUT / _SOURCE_IP /
_ECHO / _LOG。安装与部署见 docs/SSH_DEPLOY.md。
"""

import argparse
import hashlib
import os
import socket
import sys
import threading
import time

DEFAULT_TARGET = "127.0.0.1:9999"
DEFAULT_CONNECT_TIMEOUT = 3.0
# stdin 关掉之后还等多久服务端收尾（发完最后一屏就退出，别把会话挂住）
DRAIN_TIMEOUT = 2.0
# 只对这几个目标做来源地址映射：127/8 的源地址只能连本机回环
LOOPBACK_HOSTS = ("127.0.0.1", "localhost", "::1", "")

try:
    import termios
    import tty
    HAVE_TERMIOS = True
except ImportError:                 # Windows：只有管道模式（本地跑测试/调试用）
    HAVE_TERMIOS = False

try:
    import syslog
    HAVE_SYSLOG = True
except ImportError:
    HAVE_SYSLOG = False


# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------

def _env(name, default=""):
    value = os.environ.get(name)
    return default if value is None or value == "" else value


def _env_float(name, default):
    raw = _env(name, "")
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError:
        return default


def split_target(text):
    """把 host:port 拆开（支持 [::1]:9999）。"""
    text = text.strip()
    if text.startswith("["):
        host, _, rest = text[1:].partition("]")
        port = rest.lstrip(":")
    else:
        host, _, port = text.rpartition(":")
    if not host or not port:
        raise ValueError("目标地址必须写成 host:port（当前 %r）" % text)
    return host, int(port)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        prog="tetris-bridge",
        description="把 stdin/stdout 接到本机 tetris_game 的 TCP 端口（给 sshd ForceCommand 用）")
    parser.add_argument(
        "--target", default=_env("TETRIS_BRIDGE_TARGET", DEFAULT_TARGET),
        help="服务端地址 host:port（默认 %s）" % DEFAULT_TARGET)
    parser.add_argument(
        "--connect-timeout", type=float,
        default=_env_float("TETRIS_BRIDGE_CONNECT_TIMEOUT", DEFAULT_CONNECT_TIMEOUT),
        help="连接服务端的超时秒数（默认 %.0f）" % DEFAULT_CONNECT_TIMEOUT)
    parser.add_argument(
        "--source-ip", choices=("hash", "default"),
        default=_env("TETRIS_BRIDGE_SOURCE_IP", "hash"),
        help="来源地址：hash=按 SSH_CLIENT 映射出该玩家专属的 127/8 地址（默认）；"
             "default=固定 127.0.0.1")
    parser.add_argument(
        "--echo", choices=("auto", "on", "off"),
        default=_env("TETRIS_BRIDGE_ECHO", "auto"),
        help="本地回显：auto=仅当 stdin/stdout 都是终端时开（默认）")
    parser.add_argument(
        "--log", default=_env("TETRIS_BRIDGE_LOG", ""),
        help="把诊断信息追加到这个文件（默认走 syslog；都没有则静默）")
    return parser.parse_args(argv)


# ---------------------------------------------------------------------------
# 诊断输出：绝不写 stdout/stderr（那是玩家的屏幕）
# ---------------------------------------------------------------------------

def make_logger(path):
    def log(message):
        line = "%s pid=%d %s" % (time.strftime("%Y-%m-%dT%H:%M:%S"), os.getpid(), message)
        if path:
            try:
                with open(path, "a", encoding="utf-8") as handle:
                    handle.write(line + "\n")
            except OSError:
                pass
        if HAVE_SYSLOG and os.path.exists("/dev/log"):
            try:
                syslog.openlog("tetris-bridge")
                syslog.syslog(syslog.LOG_INFO, message)
            except (OSError, ValueError):
                pass
    return log


# ---------------------------------------------------------------------------
# 来源地址：让服务端的"按 IP 限流"仍然是"每个玩家一份"
# ---------------------------------------------------------------------------

def ssh_client_ip():
    """SSH 会话的真实客户端地址；拿不到返回空串。"""
    for name in ("SSH_CLIENT", "SSH_CONNECTION"):
        fields = _env(name).split()
        if fields:
            return fields[0]
    return ""


def mapped_source_address(client_ip):
    """把真实客户端地址映射成该客户端专属的 127/8 源地址；无法映射返回 None。

    IPv4 直接保留后三字节（10.1.2.3 -> 127.1.2.3）：同一客户端恒定、不同客户端
    必然不同，服务端 ntohl(sin_addr) 拿到的就是一个稳定的伪匿名标识。
    """
    if not client_ip:
        return None
    try:
        packed = socket.inet_pton(socket.AF_INET, client_ip)
    except OSError:
        packed = None
    if packed is not None:
        first, second, third, fourth = packed
        # 客户端本身就是 127/8（本机测试），或者映射结果会撞上 127.0.0.1 /
        # 127.0.0.53（systemd-resolved）/ 127.0.0.11（docker DNS）这类本机保留
        # 地址时，退回默认源：宁可和其他会话共享额度，也不要和无关的服务混在一起。
        if first == 127:
            return None
        if second == 0 and third == 0 and fourth in (1, 11, 53):
            return None
        return "127.%d.%d.%d" % (second, third, fourth)
    # IPv6（或其它写法的地址）：取摘要前两字节落到 127.255.0.0/16。
    # 会有碰撞，但那只会让两个玩家的额度合并，不会误伤或放行。
    digest = hashlib.sha256(client_ip.encode("utf-8")).digest()
    return "127.255.%d.%d" % (digest[0], digest[1])


# ---------------------------------------------------------------------------
# 连接
# ---------------------------------------------------------------------------

class ConnectError(Exception):
    """连不上服务端。"""


def open_connection(host, port, timeout, source, log):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(timeout)
        if source:
            try:
                sock.bind((source, 0))
            except OSError as exc:
                log("bind 源地址 %s 失败（%s），退回默认源地址" % (source, exc))
        sock.connect((host, port))
    except OSError as exc:
        sock.close()
        raise ConnectError("%s:%d：%s" % (host, port, exc)) from exc
    sock.settimeout(None)          # 之后是长连接，不做读超时（由 sshd 管会话寿命）
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass
    return sock


# ---------------------------------------------------------------------------
# 字节搬运
# ---------------------------------------------------------------------------

def write_all(fd, data):
    """把 data 写完（os.write 可能只写一部分，比如管道/pty 缓冲满时）。"""
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        if written <= 0:
            raise OSError("write 返回 %d" % written)
        view = view[written:]


def translate_input(data):
    """把终端输入整理成服务端要的样子：回车统一成 CRLF（服务端按它分帧）。"""
    out = bytearray()
    index = 0
    total = len(data)
    while index < total:
        byte = data[index]
        if byte == 13:                      # CR：raw 模式下按回车就是这个
            out += b"\r\n"
            if index + 1 < total and data[index + 1] == 10:
                index += 1                  # 本来就是 CRLF，别再补一个
        elif byte == 10:                    # 裸 LF：也当成一次回车
            out += b"\r\n"
        else:
            out.append(byte)
        index += 1
    return bytes(out)


def echo_input(fd, data):
    """本地回显：只回可打印字符；含 ESC 的整块不回显（方向键会把屏幕搞乱）。"""
    if b"\x1b" in data:
        return
    out = bytearray()
    for byte in data:
        if byte in (10, 13):
            out += b"\r\n"
        elif byte >= 32 and byte != 127:   # 含 UTF-8 多字节（>=128）
            out.append(byte)
    if out:
        write_all(fd, bytes(out))


def relay(stdin_fd, sock, echo, log):
    """在 stdin/stdout 与 socket 之间双向转发，直到任意一端结束。

    两个方向各一个守护线程：stdin 线程把输入翻成 CRLF 后发出去；socket 线程把
    服务端输出原样写到 fd 1。用线程而不是 select 是为了能在 Windows 上跑同一份
    代码（msvcrt 的 select 只认 socket，不认管道/文件描述符），本地就能测。
    """
    stop = threading.Event()

    def pump_socket():
        try:
            while not stop.is_set():
                chunk = sock.recv(65536)
                if not chunk:
                    log("服务端关闭了连接")
                    return
                write_all(1, chunk)
        except socket.timeout:
            log("等待服务端收尾超时（stdin 已关闭），退出")
        except OSError as exc:
            if not stop.is_set():
                log("读服务端失败: %s" % exc)
        finally:
            stop.set()

    def pump_stdin():
        try:
            while not stop.is_set():
                data = os.read(stdin_fd, 65536)
                if not data:
                    # 会话结束（sshd 关掉了 pty）：只半关闭写方向，让对向线程
                    # 把服务端最后那屏输出收完再退，别硬断。
                    log("stdin EOF，半关闭到服务端的写方向")
                    try:
                        sock.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
                    sock.settimeout(DRAIN_TIMEOUT)
                    return
                if echo:
                    echo_input(1, data)
                if b"\x03" in data:
                    log("收到 Ctrl+C，主动断开")
                    stop.set()
                    return
                payload = translate_input(data)
                if payload:
                    sock.sendall(payload)
        except OSError as exc:
            if not stop.is_set():
                log("转发输入失败: %s" % exc)
            stop.set()

    reader = threading.Thread(target=pump_socket, name="socket", daemon=True)
    writer = threading.Thread(target=pump_stdin, name="stdin", daemon=True)
    reader.start()
    writer.start()
    stop.wait()
    reader.join(0.2)
    writer.join(0.2)


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

def game_unavailable(target):
    """服务端不可用时给玩家看的一句话（带 CRLF：这时终端可能已经是 raw）。"""
    message = ("\x1b[2J\x1b[H"
               "游戏服务暂时不可用，请稍后再试。\r\n"
               "（无法连接游戏服务器 %s）\r\n" % target)
    try:
        write_all(1, message.encode("utf-8"))
    except OSError:
        pass


def main(argv=None):
    try:
        args = parse_args(argv)
    except SystemExit as exc:            # --help / 参数错误
        return int(exc.code or 0)

    try:
        host, port = split_target(args.target)
    except ValueError as exc:
        try:
            write_all(1, ("\r\n参数错误：%s\r\n" % exc).encode("utf-8"))
        except OSError:
            pass
        return 2

    log = make_logger(args.log)

    is_tty = HAVE_TERMIOS and os.isatty(0)
    echo = args.echo == "on" or (args.echo == "auto" and is_tty and os.isatty(1))

    saved = None
    if is_tty:
        try:
            saved = termios.tcgetattr(0)
            tty.setraw(0)                # 方向键必须即时到达，不能走行缓冲
        except (termios.error, OSError):
            saved = None
            is_tty = False

    sock = None
    try:
        source = None
        if args.source_ip == "hash":
            if host in LOOPBACK_HOSTS:
                source = mapped_source_address(ssh_client_ip())
            else:
                log("目标 %s 不是本机回环地址，跳过来源地址映射" % args.target)
        sock = open_connection(host, port, args.connect_timeout, source, log)
        log("会话开始 target=%s source=%s client=%s echo=%s tty=%s" %
            (args.target, source or "-", ssh_client_ip() or "-", echo, is_tty))
        relay(0, sock, echo, log)
        return 0
    except ConnectError as exc:
        log("连接失败: %s" % exc)
        game_unavailable(args.target)
        return 1
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        if saved is not None:
            try:
                termios.tcsetattr(0, termios.TCSADRAIN, saved)
            except (termios.error, OSError):
                pass


if __name__ == "__main__":
    sys.exit(main())
