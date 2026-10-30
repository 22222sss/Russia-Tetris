#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""游戏负载压测：真实登录 -> 开局 -> 持续按键，让每个连接产生真实 CPU 开销。

为什么不用 load_test.py
----------------------
load_test.py 只建连、不发指令。10 万连接实测服务端只占 20%~30% 单核
（见 README「多进程 worker 实测」），根本测不出多核收益。
真实游戏里每次按键服务端都要 strcmp + 整屏重绘（每次 8~10 个 send），
这才是 CPU 大户。

为什么只按左/右、不按下
----------------------
下键会让方块快速堆叠，几十秒内就 GAME OVER，玩家就没负载了；
左/右每次都会触发 DrawSpace + DrawBlock 重绘，但不加速堆叠。
因此单轮测试建议 <= 30 秒（再长重力也会把棋盘堆满）。

用法
----
    # 1) 先在服务端工作目录造账号
    python3 play_test.py --seed 200

    # 2) 服务端起来后加压
    python3 play_test.py --players 200 --seconds 15 --key-delay 0.1 --json /tmp/play.json
"""

import argparse
import asyncio
import json
import os
import sys
import time

KEY_LEFT = b"\x1b[D"
KEY_RIGHT = b"\x1b[C"

HEADER = ("Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
          "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
          "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores")

USER_FMT = "play%04d"
PWD_FMT = "pw%04d"


class AsyncConn:
    """带累积缓冲的读端：在原始字节流里查找文本片段。

    服务端每段文字都是"定位 + 颜色 + 文本"一次 send 发出来的，文本在流里连续，
    所以直接在原始字节里找即可，不需要剥离 ANSI。缓冲按需裁剪，避免游戏重绘
    把内存堆爆。
    """

    def __init__(self, reader):
        self.reader = reader
        self.buf = bytearray()
        self.mark = 0

    async def wait_for(self, needle, timeout):
        nb = needle.encode("utf-8")
        deadline = time.monotonic() + timeout
        while True:
            idx = self.buf.find(nb, self.mark)
            if idx >= 0:
                self.mark = idx + len(nb)
                if self.mark > 65536:
                    del self.buf[:self.mark]
                    self.mark = 0
                return True
            remain = deadline - time.monotonic()
            if remain <= 0:
                return False
            try:
                chunk = await asyncio.wait_for(self.reader.read(65536), timeout=remain)
            except asyncio.TimeoutError:
                return False
            if not chunk:
                return False
            self.buf += chunk


async def drain(reader, stats, stop):
    """持续读走服务端输出。不读的话服务端的发送缓冲会堆到上限并被判死连接。"""
    while not stop.is_set():
        try:
            chunk = await reader.read(65536)
        except Exception:
            return
        if not chunk:
            return
        stats["bytes_rx"] += len(chunk)


async def login_and_start(conn, writer, name, pwd, difficulty, timeout):
    if not await conn.wait_for("1. 注册帐号", timeout):
        return False
    writer.write(b"2\r\n")              # 登录
    if not await conn.wait_for("请输入用户名", timeout):
        return False
    writer.write(name.encode("utf-8") + b"\r\n")
    if not await conn.wait_for("请输入密码", timeout):
        return False
    writer.write(pwd.encode("utf-8") + b"\r\n")
    if not await conn.wait_for("3. 开始游戏", timeout):
        return False
    writer.write(b"3\r\n")              # 开始游戏 -> 难度菜单
    if not await conn.wait_for("1. 简单模式", timeout):
        return False
    writer.write(str(difficulty).encode() + b"\r\n")
    if not await conn.wait_for("Score:", timeout):
        return False
    return True


async def run_player(idx, args, stats):
    name = USER_FMT % idx
    pwd = PWD_FMT % idx

    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(args.host, args.port), timeout=args.connect_timeout)
    except Exception:
        stats["errors"] += 1
        return

    conn = AsyncConn(reader)
    try:
        ok = await login_and_start(conn, writer, name, pwd, args.difficulty, args.timeout)
        if not ok:
            stats["errors"] += 1
            return
        stats["ready"] += 1

        stop = asyncio.Event()
        drain_task = asyncio.create_task(drain(reader, stats, stop))

        end = time.monotonic() + args.seconds
        i = idx                     # 让不同玩家起始方向错开，避免整齐划一
        while time.monotonic() < end:
            writer.write(KEY_LEFT if (i & 1) == 0 else KEY_RIGHT)
            i += 1
            stats["keys"] += 1
            await asyncio.sleep(args.key_delay)

        stop.set()
        drain_task.cancel()
    except Exception:
        stats["errors"] += 1
    finally:
        try:
            writer.close()
        except Exception:
            pass


def seed_users(count, path):
    """直接往 CSV 追加账号：省掉 N 次注册往返，把时间留给真正的游戏负载。"""
    try:
        empty = os.path.getsize(path) == 0
    except OSError:
        empty = True

    with open(path, "a", encoding="utf-8") as f:
        if empty:
            f.write(HEADER + "\n")
        for i in range(count):
            f.write("%s,%s,0,EASY,,0,NORMAL,,0,DIFFICULT,,\n"
                    % (USER_FMT % i, PWD_FMT % i))
    print("已写入 %d 个账号 -> %s" % (count, path), flush=True)


async def run_all(args):
    stats = {"ready": 0, "keys": 0, "bytes_rx": 0, "errors": 0}

    started = time.monotonic()
    await asyncio.gather(*[run_player(i, args, stats) for i in range(args.players)])
    elapsed = time.monotonic() - started

    result = {
        "players": args.players,
        "ready": stats["ready"],
        "keys": stats["keys"],
        "bytes_rx": stats["bytes_rx"],
        "errors": stats["errors"],
        "elapsed_seconds": round(elapsed, 3),
        "keys_per_second": round(stats["keys"] / elapsed, 1) if elapsed else 0,
        "rx_kb_per_second": round(stats["bytes_rx"] / 1024.0 / elapsed, 1) if elapsed else 0,
        "config": {
            "host": args.host, "port": args.port,
            "seconds": args.seconds, "key_delay": args.key_delay,
            "difficulty": args.difficulty,
        },
    }
    return result


def main():
    parser = argparse.ArgumentParser(description="游戏负载压测客户端")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--players", type=int, default=200)
    parser.add_argument("--seconds", type=float, default=15.0)
    parser.add_argument("--key-delay", type=float, default=0.1,
                        help="每个玩家两次按键之间的间隔（秒）")
    parser.add_argument("--difficulty", type=int, default=3,
                        help="1=简单 2=普通 3=困难（默认困难：重力 0.2s/格）")
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="单个登录/开局步骤的等待上限（秒）")
    parser.add_argument("--connect-timeout", type=float, default=10.0)
    parser.add_argument("--userdata", default="userdata.csv")
    parser.add_argument("--seed", type=int, default=0,
                        help="只造账号不发压：play_test.py --seed 200")
    parser.add_argument("--json", default="",
                        help="把结果写成 JSON")
    args = parser.parse_args()

    if args.seed > 0:
        seed_users(args.seed, args.userdata)
        return 0

    result = asyncio.run(run_all(args))

    print("玩家: %d/%d 就绪, 按键 %d 次 (%.1f/s), 收到 %.1f KB/s, 错误 %d, 耗时 %.1fs"
          % (result["ready"], result["players"], result["keys"],
             result["keys_per_second"], result["rx_kb_per_second"],
             result["errors"], result["elapsed_seconds"]), flush=True)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=2)

    if result["ready"] == 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
