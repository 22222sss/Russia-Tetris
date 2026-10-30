#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""采样服务端进程树的 RSS / 线程数 / fd 数 / CPU 时间，供 bench.sh 使用。

多 worker 模式下服务端是"父进程 + N 个子进程"，父进程几乎不干活，
所以必须把整棵树的指标加起来，否则 CPU/RSS 会接近 0，曲线完全失真。

用法:
    python3 sample_proc.py <pid> <输出json> [采样间隔秒]

收到 SIGTERM/SIGINT 或进程退出时写盘。输出格式:
    {"samples": [{"t": 1690000000.0, "rss_kb": 1234, "threads": 8,
                  "fds": 5678, "cpu_ticks": 9012, "procs": 1}, ...]}
"""
import json
import os
import signal
import sys
import time

_stop = False


def _handle(signum, frame):
    global _stop
    _stop = True


def read_status(pid, key):
    try:
        with open('/proc/%d/status' % pid, 'r') as f:
            for line in f:
                if line.startswith(key):
                    return int(line.split()[1])
    except OSError:
        pass
    return None


def read_fds(pid):
    try:
        return len(os.listdir('/proc/%d/fd' % pid))
    except OSError:
        return None


def read_ticks(pid):
    try:
        with open('/proc/%d/stat' % pid, 'r') as f:
            parts = f.read().split()
        return int(parts[13]) + int(parts[14])
    except (OSError, IndexError):
        return None


def read_ppid(pid):
    """从 /proc/<pid>/stat 取父进程号。

    comm 字段可能带空格和括号，所以从最后一个 ')' 之后再切，
    避免简单 split 出错。
    """
    try:
        with open('/proc/%d/stat' % pid, 'r') as f:
            data = f.read()
        tail = data[data.rfind(')') + 2:].split()
        return int(tail[1])   # tail[0] = state, tail[1] = ppid
    except (OSError, IndexError, ValueError):
        return None


def descendants(root_pid):
    """root_pid 及其所有后代（BFS）。"""
    parent_of = {}
    for entry in os.listdir('/proc'):
        if not entry.isdigit():
            continue
        child = int(entry)
        ppid = read_ppid(child)
        if ppid is not None:
            parent_of.setdefault(ppid, []).append(child)

    result = [root_pid]
    queue = [root_pid]
    while queue:
        current = queue.pop(0)
        for child in parent_of.get(current, []):
            result.append(child)
            queue.append(child)
    return result


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    pid = int(sys.argv[1])
    out = sys.argv[2]
    interval = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

    signal.signal(signal.SIGTERM, _handle)
    signal.signal(signal.SIGINT, _handle)

    samples = []
    while not _stop and os.path.isdir('/proc/%d' % pid):
        pids = descendants(pid)

        rss = 0
        threads = 0
        fds = 0
        ticks = 0
        for p in pids:
            value = read_status(p, 'VmRSS:')
            if value:
                rss += value
            value = read_status(p, 'Threads:')
            if value:
                threads += value
            value = read_fds(p)
            if value:
                fds += value
            value = read_ticks(p)
            if value:
                ticks += value

        samples.append({
            't': time.time(),
            'rss_kb': rss,
            'threads': threads,
            'fds': fds,
            'cpu_ticks': ticks,
            'procs': len(pids),
        })
        time.sleep(interval)

    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'samples': samples}, f, ensure_ascii=False)
    print('采样结束，共 %d 个样本 -> %s' % (len(samples), out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
