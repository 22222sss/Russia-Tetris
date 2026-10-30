#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""汇总 play_remote.sh 的每轮结果：就绪数、按键/下行、服务端 CPU。

为什么单独算 CPU 窗口
====================
样本从"服务端启动"就开始采，包含了 800 个账号的登录阶段（PBKDF2，几十秒）；
而我们要比较的是**游戏阶段**的多 worker 扩展性。所以这里只取样本的最后
`--seconds` 秒（play_test 的游玩窗口）来算 CPU，避免登录成本稀释结果。

用法:
    python3 summarize_play.py <结果目录> <玩家数> <游戏秒数>

输入: <结果目录>/play-w<W>-r<R>.json + samples-w<W>-r<R>.json
输出: 标准输出表格 + summary.md + 每轮一条 ::notice::
"""

import glob
import json
import os
import sys


def load_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def cpu_percent(samples, window_seconds):
    """取最后 window_seconds（+1s 余量）里 **10 秒滑动窗口的最大 CPU 均值**。

    不直接对整个窗口求平均：每个玩家的登录/开局时间不同，窗口尾部负载会衰减，
    直接平均会把"满负荷阶段"稀释掉。峰值 10s 更能代表游戏阶段的真实占用。
    """
    if not samples:
        return None
    end = samples[-1]["t"]
    window = [s for s in samples if s["t"] >= end - window_seconds - 1.0]
    if len(window) < 2:
        return None
    try:
        hz = os.sysconf("SC_CLK_TCK")
    except (ValueError, OSError, AttributeError):
        hz = 100

    span = window[-1]["t"] - window[0]["t"]
    use_sliding = span > 12.0
    best = None
    for i in range(len(window)):
        for j in range(i + 1, len(window)):
            dt = window[j]["t"] - window[i]["t"]
            if dt <= 0:
                continue
            if use_sliding:
                if dt < 8.0:
                    continue
                if dt > 12.0:
                    break
            ti, tj = window[i].get("cpu_ticks"), window[j].get("cpu_ticks")
            if ti is None or tj is None:
                continue
            rate = (tj - ti) / hz / dt * 100.0
            if best is None or rate > best:
                best = rate
    return round(best, 1) if best is not None else None


def parse_tag(tag):
    # tag = w1-r2
    workers = tag.split("-")[0].lstrip("w")
    round_no = tag.split("-")[1].lstrip("r") if "-" in tag else "?"
    return workers, round_no


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    out_dir = sys.argv[1]
    players = int(sys.argv[2])
    seconds = float(sys.argv[3])

    rows = []
    for path in sorted(glob.glob(os.path.join(out_dir, "play-*.json"))):
        tag = os.path.basename(path)[len("play-"):-len(".json")]
        workers, round_no = parse_tag(tag)
        play = load_json(path) or {}

        samples = (load_json(os.path.join(out_dir, "samples-%s.json" % tag)) or {}).get("samples", [])
        cpu = cpu_percent(samples, seconds)
        rss = max([s.get("rss_kb") or 0 for s in samples], default=0) / 1024.0
        fds = max([s.get("fds") or 0 for s in samples], default=0)
        procs = samples[-1].get("procs") if samples else None

        rows.append({
            "workers": workers,
            "round": round_no,
            "tag": tag,
            "ready": play.get("ready"),
            "keys": play.get("keys"),
            "keys_per_second": play.get("keys_per_second"),
            "rx_kb_per_second": play.get("rx_kb_per_second"),
            "cpu": cpu,
            "rss_mb": round(rss, 1) if samples else None,
            "fds": fds or None,
            "procs": procs,
        })

    if not rows:
        print("没有找到 play-*.json，无法汇总")
        return 1

    header = ("workers | 轮次 | 就绪 | 按键/秒 | 下行KB/s | 服务端CPU% | RSS MB | fd | 进程数")
    print(header)
    print("-" * len(header))
    for row in rows:
        print("%7s | %4s | %4s | %8s | %8s | %10s | %6s | %4s | %4s" % (
            row["workers"], row["round"],
            "%s/%s" % (row["ready"], players),
            row["keys_per_second"], row["rx_kb_per_second"],
            row["cpu"], row["rss_mb"], row["fds"], row["procs"]))

    # CI 里日志读不到，每轮回传一条注解
    for row in rows:
        print("::notice::远程游戏负载[workers=%s 第%s轮] 就绪 %s/%s，按键 %s/s，下行 %s KB/s，"
              "服务端 CPU %s%%（占满一核=100%%），RSS %s MB，进程数 %s" % (
                  row["workers"], row["round"], row["ready"], players,
                  row["keys_per_second"], row["rx_kb_per_second"],
                  row["cpu"], row["rss_mb"], row["procs"]))

    lines = ["# 远程游戏负载压测", "",
             "- 玩家数: %d" % players,
             "- 游玩时长: %.0fs" % seconds,
             "- CPU 取游戏窗口内 10 秒滑动窗口的最大值（不含登录阶段，避免尾部负载衰减稀释）",
             "",
             "| workers | 轮次 | 就绪 | 按键/秒 | 下行 KB/s | 服务端 CPU% | RSS MB | fd | 进程数 |",
             "|---|---|---|---|---|---|---|---|---|"]
    for row in rows:
        lines.append("| %s | %s | %s/%s | %s | %s | %s | %s | %s | %s |" % (
            row["workers"], row["round"], row["ready"], players,
            row["keys_per_second"], row["rx_kb_per_second"], row["cpu"],
            row["rss_mb"], row["fds"], row["procs"]))
    lines += ["",
              "判读要点：",
              "- 同一负载下，加 worker 后 CPU 应该更低（同样的按键量分摊到多核），或下行/按键速率更高；",
              "- 如果加 worker 后 CPU 反而升高、按键速率不涨，说明瓶颈不在服务端 CPU（可能是客户端/带宽）；",
              "- 单 worker 最高只能占满 1 核（100%），超过 100% 的合计 CPU 只可能来自多 worker。"]
    with open(os.path.join(out_dir, "summary.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\n已写入 %s" % os.path.join(out_dir, "summary.md"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
