#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把一次游戏负载压测的结果归纳成一行指标（play_bench.sh 调用）。

为什么 CPU 要分"游戏窗口"和"全程"两个口径
-----------------------------------------
采样从服务端启动就开始，包含 N 个账号的登录阶段。登录的主要成本是 PBKDF2，
跑在 OpenSSL 的汇编实现里，**不受 -O2 影响**。如果拿全程 CPU 比较优化级别
（或任何游戏逻辑改动），登录成本会把差异稀释掉；而拿"游戏窗口"（所有玩家
都已在按键的交集区间）就能只看真正被优化的那段代码。
所以两个都打印：游戏窗口用于同机 A/B，全程用于与历史数据（-O0 时代）对齐。

每按键 CPU 时间
---------------
`µs/按键 = 窗口内 CPU% × 10000 / 窗口内按键速率`，与玩家数、压测时长无关，
是跨 runner、跨优化级别最稳的比较量（100% = 占满一核）。

用法:
    python3 gameplay_stats.py <play.json> <samples.json> <label> [退出码]

`label` 会原样出现在 ::notice:: 里（例如 "workers=1 opt=O2"）。
"""

import json
import os
import sys


def load_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def cpu_percent(samples, start=None, end=None):
    """按 CPU tick 差算平均占用率；给了窗口就只取窗口内的样本。

    返回 (占用率, 是否用了窗口) —— 窗口内样本不足 2 个时退回全程。
    """
    if not samples:
        return None, False

    use = samples
    windowed = False
    if start is not None and end is not None:
        sel = [s for s in samples if start <= s["t"] <= end]
        if len(sel) >= 2 and sel[-1]["t"] > sel[0]["t"]:
            use = sel
            windowed = True

    ticks = [(s["t"], s.get("cpu_ticks")) for s in use
             if s.get("cpu_ticks") is not None]
    if len(ticks) < 2 or ticks[-1][0] <= ticks[0][0]:
        return None, False

    try:
        hz = os.sysconf("SC_CLK_TCK")
    except (ValueError, OSError, AttributeError):
        hz = 100
    rate = (ticks[-1][1] - ticks[0][1]) / hz / (ticks[-1][0] - ticks[0][0]) * 100.0
    return round(rate, 1), windowed


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2

    play_path, samp_path, label = sys.argv[1], sys.argv[2], sys.argv[3]
    rc = sys.argv[4] if len(sys.argv) > 4 else "0"

    play = load_json(play_path) or {}
    samples = (load_json(samp_path) or {}).get("samples", [])
    samples = [s for s in samples if s]

    window = play.get("game_window") or {}
    cpu_win, windowed = cpu_percent(samples, window.get("start"), window.get("end"))
    cpu_all, _ = cpu_percent(samples)

    rss = fds = procs = None
    if samples:
        rss = round(max(s.get("rss_kb") or 0 for s in samples) / 1024.0, 1)
        fds = max([s.get("fds") or 0 for s in samples]) or None
        procs = samples[-1].get("procs")

    # 每按键 CPU：优先用窗口内的按键速率，没有就用全程速率
    rate = play.get("window_keys_per_second") or play.get("keys_per_second")
    us_per_key = None
    if cpu_win is not None and rate:
        us_per_key = round(cpu_win * 10000.0 / rate, 1)

    if cpu_win is None:
        cpu_win_txt = "?（无采样）"
    elif windowed:
        cpu_win_txt = "%s%%（游戏窗口）" % cpu_win
    else:
        cpu_win_txt = "%s%%（无窗口，取全程）" % cpu_win
    cpu_all_txt = "%s%%" % cpu_all if cpu_all is not None else "?"
    line = ("[%s] 玩家 %s/%s 就绪, 按键 %s 次（%s/s 全程 / %s/s 游戏内）, 下行 %s KB/s, "
            "服务端 CPU %s / %s（全程）, 每按键 %s us, RSS %s MB, fd %s, 进程数 %s, 退出码 %s"
            % (label,
               play.get("ready", "?"), play.get("players", "?"),
               play.get("keys", "?"),
               play.get("keys_per_second", "?"),
               play.get("window_keys_per_second", "?"),
               play.get("rx_kb_per_second", "?"),
               cpu_win_txt, cpu_all_txt,
               us_per_key, rss, fds, procs, rc))

    print(line, flush=True)
    # CI 里日志读不到，注解是唯一的结果通道（每个 step 的 notice 上限 10 条）
    print("::notice::" + line, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
