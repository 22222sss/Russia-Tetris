#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""采样指定进程的 RSS / 线程数 / fd 数 / CPU 时间，供 bench.sh 使用。

用法:
    python3 sample_proc.py <pid> <输出json> [采样间隔秒]

收到 SIGTERM/SIGINT 或进程退出时写盘。输出格式:
    {"samples": [{"t": 1690000000.0, "rss_kb": 1234, "threads": 8,
                  "fds": 5678, "cpu_ticks": 9012}, ...]}
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
        samples.append({
            't': time.time(),
            'rss_kb': read_status(pid, 'VmRSS:'),
            'threads': read_status(pid, 'Threads:'),
            'fds': read_fds(pid),
            'cpu_ticks': read_ticks(pid),
        })
        time.sleep(interval)

    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'samples': samples}, f, ensure_ascii=False)
    print('采样结束，共 %d 个样本 -> %s' % (len(samples), out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
