#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""合并 bench 的多个客户端分片结果与服务端采样数据。

用法:
    python3 merge_results.py <bench-results/时间戳> <分片数>

读取目录下的 result-0.json ... result-N-1.json 与 server_samples.json，
输出合并后的 result.json（失败时退出码 1）。
"""
import json
import os
import sys


def load_shards(out, n):
    shards = []
    for i in range(n):
        path = os.path.join(out, 'result-%d.json' % i)
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                shards.append(json.load(f))
    return shards


def server_from_samples(path):
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        samples = [s for s in json.load(f).get('samples', []) if s]
    if not samples:
        return None
    rss = [s['rss_kb'] for s in samples if s.get('rss_kb')]
    fds = [s['fds'] for s in samples if s.get('fds')]
    ticks = [(s['t'], s['cpu_ticks']) for s in samples if s.get('cpu_ticks') is not None]
    cpu = None
    if len(ticks) >= 2 and ticks[-1][0] > ticks[0][0]:
        hz = os.sysconf('SC_CLK_TCK') if hasattr(os, 'sysconf') else 100
        cpu = round(((ticks[-1][1] - ticks[0][1]) / hz) / (ticks[-1][0] - ticks[0][0]) * 100.0, 1)
    return {
        'rss_peak_mb': round(max(rss) / 1024.0, 1) if rss else None,
        'rss_last_mb': round(rss[-1] / 1024.0, 1) if rss else None,
        'fds_peak': max(fds) if fds else None,
        'fds_last': fds[-1] if fds else None,
        'threads_last': samples[-1].get('threads'),
        'cpu_percent_avg': cpu,
        'samples': len(samples),
    }


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out, n = sys.argv[1], int(sys.argv[2])

    shards = load_shards(out, n)
    if not shards:
        print('没有可合并的分片结果')
        return 1

    connected = sum(s['result']['connected'] for s in shards)
    failed = sum(s['result']['failed'] for s in shards)
    reasons = {}
    for s in shards:
        for k, v in s['result']['failed_by_reason'].items():
            reasons[k] = reasons.get(k, 0) + v
    elapsed = max(s['result']['elapsed_seconds'] for s in shards)

    curves = [s['result'].get('rate_per_10s') or [] for s in shards]
    width = max((len(c) for c in curves), default=0)
    curve = [sum((c[i] if i < len(c) else 0) for c in curves) for i in range(width)]

    first = shards[0]
    if len(shards) == 1:
        latency = first['result'].get('latency_seconds') or {
            'p50': None, 'p95': None, 'p99': None, 'max': None}
    else:
        # 多分片时各进程独立统计，不做近似合并
        latency = {'p50': None, 'p95': None, 'p99': None, 'max': None}

    server = server_from_samples(os.path.join(out, 'server_samples.json'))
    if server is None:
        server = first.get('server', {})

    merged = {
        'config': {
            'target': sum(s['config']['target'] for s in shards),
            'concurrency': sum(s['config']['concurrency'] for s in shards),
            'read_check': first['config']['read_check'],
            'hold_seconds': first['config']['hold_seconds'],
            'host': first['config'].get('host'),
            'port': first['config'].get('port'),
            'clients': n,
            'shards': len(shards),
        },
        'result': {
            'connected': connected,
            'failed': failed,
            'failed_by_reason': dict(sorted(reasons.items())),
            'attempted': connected + failed,
            'success_rate': round(connected / (connected + failed), 6) if (connected + failed) else 0.0,
            'received_data': sum(s['result']['received_data'] for s in shards),
            'no_data': sum(s['result']['no_data'] for s in shards),
            'elapsed_seconds': round(elapsed, 3),
            'rate_per_second': round(connected / elapsed, 1) if elapsed else 0.0,
            'latency_seconds': latency,
            'rate_per_10s': curve,
        },
        'server': server,
        'timestamp': first.get('timestamp'),
    }

    with open(os.path.join(out, 'result.json'), 'w', encoding='utf-8') as f:
        json.dump(merged, f, ensure_ascii=False, indent=2)

    print('合并 %d 个分片: 成功 %d / 失败 %d, 耗时 %.1fs, %.0f 连接/秒'
          % (len(shards), connected, failed, elapsed, connected / elapsed if elapsed else 0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
