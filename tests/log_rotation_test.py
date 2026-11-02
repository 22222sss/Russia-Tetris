#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""日志轮转的自动化测试（需要真二进制）。

为什么单独测：`log.txt` 以前只涨不缩，一个跑上几个月的实例能把磁盘写满，
而"磁盘满了之后 SQLite 写失败"会在最不该出事的时候出现。改成 spdlog 的
rotating sink 之后，"轮转到底有没有发生"只能从文件系统上证明，所以这里断言：

1. `TETRIS_LOG_DIR` 真的生效（日志写进指定目录，而不是工作目录）；
2. `TETRIS_LOG_MAX_BYTES` 真的被读到：写满后出现 `log.1.txt`（不是一直写一个文件）；
3. 文件数 = 当前文件 + 不超过 `TETRIS_LOG_MAX_FILES` 份历史
   （spdlog 的 max_files 指**轮转出去的份数**，当前 `log.txt` 另算）；
4. 每个文件都不超过上限的 ~1.5 倍（说明是"接近上限就轮转"，不是攒到很大才转）；
5. `log.txt` 始终是"当前文件"的名字（运维按这个名字找日志）。

用法（在 src/ 下跑，和 CI 一致）：
    python3 ../tests/log_rotation_test.py --bin ./tetris_game
"""

import argparse
import glob
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


class Failure(Exception):
    """断言失败。"""


def annotate(level, message):
    msg = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::%s::%s" % (level, msg), flush=True)


def free_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def wait_for_port(port, timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def drive_connections(port, count):
    """连上就断开：每条连接在 info 级别会写"连接/断开"两行日志。"""
    done = 0
    for _ in range(count):
        try:
            conn = socket.create_connection(("127.0.0.1", port), timeout=2.0)
            conn.settimeout(0.2)
            try:
                conn.recv(256)          # 首屏菜单，顺便确认服务端真的在说话
            except OSError:
                pass
            conn.close()
            done += 1
        except OSError as exc:
            raise Failure("第 %d 条连接失败：%s" % (done + 1, exc))
    return done


def test_rotation(binary, workdir, connections, max_bytes, max_files):
    log_dir = os.path.join(workdir, "logs")
    os.makedirs(log_dir, exist_ok=True)
    port = free_port()
    db_path = os.path.join(workdir, "tetris.db")

    env = dict(os.environ)
    env.update({
        "TETRIS_PORT": str(port),
        "TETRIS_BIND": "127.0.0.1",
        "TETRIS_WORKERS": "1",
        "TETRIS_DB": db_path,
        "TETRIS_LOG_DIR": log_dir,
        "TETRIS_LOG_MAX_BYTES": str(max_bytes),
        "TETRIS_LOG_MAX_FILES": str(max_files),
        # info 级别：每条连接写两行；同时打开 flush_on，保证进程退出前都落盘
        "TETRIS_LOG_LEVEL": "info",
    })
    stdout_path = os.path.join(workdir, "server.out")
    stdout = open(stdout_path, "wb")
    proc = subprocess.Popen([binary], cwd=workdir, env=env,
                            stdout=stdout, stderr=subprocess.STDOUT)
    try:
        if not wait_for_port(port):
            raise Failure("服务端 30 秒内没有监听 127.0.0.1:%d；输出：%s"
                          % (port, tail(stdout_path)))
        drive_connections(port, connections)
        # 异步 logger 有队列：等它把最后几条写出去，再看文件
        time.sleep(1.5)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        stdout.close()

    files = sorted(glob.glob(os.path.join(log_dir, "log*.txt")))
    names = [os.path.basename(path) for path in files]
    sizes = {os.path.basename(path): os.path.getsize(path) for path in files}
    print("日志文件: %s" % sizes, flush=True)

    if "log.txt" not in names:
        raise Failure("日志目录里没有 log.txt（TETRIS_LOG_DIR 没生效？目录内容：%r）"
                      % sorted(os.listdir(log_dir)))
    if len(files) < 3:
        raise Failure("没有看到至少两次轮转：只有 %r（写了 %s 字节，上限 %d）——"
                      "TETRIS_LOG_MAX_BYTES 是不是没被读到？"
                      % (names, sizes.get("log.txt"), max_bytes))
    # spdlog 的 max_files 是"轮转出去的份数"：当前文件 + 最多 max_files 份历史。
    # 两边留一档余量（不同版本对 max_files 的解释可能差一），只守住"不会无限涨"。
    if len(files) > max_files + 1:
        raise Failure("文件数 %d 超过 max_files+1=%d（TETRIS_LOG_MAX_FILES=%d）：%r"
                      % (len(files), max_files + 1, max_files, names))
    if "log.1.txt" not in names:
        raise Failure("没有 log.1.txt（spdlog 的轮转命名是 log.N.txt）：%r" % names)

    limit = int(max_bytes * 1.5)
    oversized = {name: size for name, size in sizes.items() if size > limit}
    if oversized:
        raise Failure("有文件超过上限的 1.5 倍（%d 字节）：%r —— 轮转触发太晚"
                      % (limit, oversized))

    # 日志应该落在 TETRIS_LOG_DIR，而不是启动目录
    stray = os.path.join(workdir, "log.txt")
    if os.path.exists(stray):
        raise Failure("工作目录里也有一份 log.txt：TETRIS_LOG_DIR 没有生效")

    if not os.path.exists(db_path):
        raise Failure("TETRIS_DB 指定的库没有建出来：%s" % db_path)

    return "写 %d 条连接后：%r，合计 %d 字节" % (
        connections, sizes, sum(sizes.values()))


def tail(path, limit=1200):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
        return data[-limit:].decode("utf-8", "replace")
    except OSError:
        return "<读不到>"


def main():
    parser = argparse.ArgumentParser(description="日志轮转测试")
    parser.add_argument("--bin", default="./tetris_game", help="服务端二进制")
    parser.add_argument("--workdir", default="", help="工作目录（默认临时目录）")
    parser.add_argument("--connections", type=int, default=500,
                        help="制造多少条连接（每条在 info 级别写两行日志）")
    parser.add_argument("--max-bytes", type=int, default=4096, help="单份上限")
    parser.add_argument("--max-files", type=int, default=3, help="保留份数")
    args = parser.parse_args()

    if not os.path.exists(args.bin):
        annotate("error", "找不到 %s（先在 src/ 下 make）" % args.bin)
        return 1
    # 换了 cwd 之后相对路径会失效（HANDOVER 6.43），先转绝对路径
    binary = os.path.abspath(args.bin)
    workdir = args.workdir or tempfile.mkdtemp(prefix="tetris-logrotate-")
    os.makedirs(workdir, exist_ok=True)

    print("二进制: %s\n工作目录: %s\n上限: %d 字节 × %d 份"
          % (binary, workdir, args.max_bytes, args.max_files), flush=True)
    try:
        detail = test_rotation(binary, workdir, args.connections,
                               args.max_bytes, args.max_files)
    except Failure as exc:
        print("FAIL: %s" % exc, flush=True)
        annotate("error", "日志轮转测试失败: %s" % exc)
        return 1

    print("PASS 日志轮转：%s" % detail, flush=True)
    annotate("notice", "日志轮转生效（%s）" % detail)
    shutil.rmtree(workdir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
