#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""登录吞吐基准：并发真实登录（用户名 -> 密码 -> 主菜单），测延迟与吞吐。

为什么单独测登录
================
密码校验是 PBKDF2-HMAC-SHA256（10 万次迭代），故意做得慢；它跑在异步文件线程上，
**单线程串行**。所以登录高峰的吞吐上限 ≈ 1 / 单次哈希耗时，跟游戏帧率无关。
这个脚本只走登录阶段（不开始游戏），用来量化这条路径的成本和优化效果。

用法
====
    # 1) 造账号（密码直接写成哈希，避免登录时触发明文迁移）
    python3 login_bench.py --seed 200 --userdata userdata.csv

    # 2) 服务端起来后压登录
    python3 login_bench.py --players 200 --port 10099 --json /tmp/login.json

CI 里由 tests/login_bench.sh 调用（每轮回传 ::notice:: 注解，日志读不到也能看结果）。
只依赖标准库。
"""

import argparse
import asyncio
import json
import os
import sys
import time

import credential

USER_FMT = "lb%04d"
PWD_FMT = "pw%04d"

HEADER = ("Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
          "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
          "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores")


class AsyncConn:
    """在原始字节流里等文本片段出现（服务端文本是连续 send 的，不用剥 ANSI）。"""

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


def percentile(values, q):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = int(round(q * (len(ordered) - 1)))
    index = min(len(ordered) - 1, max(0, index))
    return ordered[index]


async def login_once(idx, args, stats, latencies):
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
        if not await conn.wait_for("1. 注册帐号", args.timeout):
            stats["errors"] += 1
            return
        writer.write(b"2\r\n")                     # 登录
        if not await conn.wait_for("请输入用户名", args.timeout):
            stats["errors"] += 1
            return
        writer.write(name.encode("utf-8") + b"\r\n")
        if not await conn.wait_for("请输入密码", args.timeout):
            stats["errors"] += 1
            return

        # 计时起点：密码发出；终点：主菜单出现（中间包含服务端 PBKDF2 校验）
        started = time.monotonic()
        writer.write(pwd.encode("utf-8") + b"\r\n")
        if not await conn.wait_for("3. 开始游戏", args.timeout):
            stats["errors"] += 1
            return
        latencies.append(time.monotonic() - started)

        stats["ready"] += 1
    except Exception:
        stats["errors"] += 1
    finally:
        try:
            writer.close()
        except Exception:
            pass


def seed_users(count, path):
    """直接写哈希账号：省掉 N 次注册往返，且避免登录时触发迁移。"""
    try:
        empty = os.path.getsize(path) == 0
    except OSError:
        empty = True

    with open(path, "a", encoding="utf-8") as f:
        if empty:
            f.write(HEADER + "\n")
        for i in range(count):
            f.write("%s,%s,0,EASY,,0,NORMAL,,0,DIFFICULT,,\n"
                    % (USER_FMT % i, credential.hash_password(PWD_FMT % i)))
    print("已写入 %d 个账号（密码为加盐哈希）-> %s" % (count, path), flush=True)


async def run_all(args):
    stats = {"ready": 0, "errors": 0}
    latencies = []

    started = time.monotonic()
    await asyncio.gather(*[login_once(i, args, stats, latencies) for i in range(args.players)])
    elapsed = time.monotonic() - started

    ready = stats["ready"]
    result = {
        "players": args.players,
        "ready": ready,
        "errors": stats["errors"],
        "elapsed_seconds": round(elapsed, 3),
        "logins_per_second": round(ready / elapsed, 2) if elapsed > 0 else 0.0,
        "latency_p50": round(percentile(latencies, 0.50), 3),
        "latency_p95": round(percentile(latencies, 0.95), 3),
        "latency_max": round(max(latencies), 3) if latencies else 0.0,
        "label": args.label,
    }
    return result


def print_result(result):
    line = ("登录基准[%s] 就绪 %d/%d，总耗时 %.2fs，%.1f 登录/秒，"
            "延迟 p50=%.2fs p95=%.2fs max=%.2fs，错误 %d"
            % (result["label"] or "-", result["ready"], result["players"],
               result["elapsed_seconds"], result["logins_per_second"],
               result["latency_p50"], result["latency_p95"],
               result["latency_max"], result["errors"]))
    print(line, flush=True)
    # CI 日志读不到，注解是唯一能看到结果的通道
    print("::notice::" + line, flush=True)


def main():
    parser = argparse.ArgumentParser(description="俄罗斯方块服务端登录吞吐基准")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--players", type=int, default=200)
    parser.add_argument("--seed", type=int, default=0,
                        help="只造账号不压测：--seed 200")
    parser.add_argument("--userdata", default="userdata.csv",
                        help="userdata.csv 路径（相对服务端工作目录）")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="单个登录步骤的等待上限（秒）")
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--json", default="", help="把结果写成 JSON")
    parser.add_argument("--label", default="", help="结果标签（如 workers=1 / 第 2 轮）")
    args = parser.parse_args()

    if args.seed > 0:
        seed_users(args.seed, args.userdata)
        return 0

    result = asyncio.run(run_all(args))
    print_result(result)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(result, fh, ensure_ascii=False, indent=2)

    if result["ready"] != result["players"] or result["errors"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
