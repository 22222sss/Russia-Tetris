#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
俄罗斯方块服务端并发压测客户端

用法（与旧版兼容，前 4 个位置参数保持不变）:
    python3 load_test.py [目标连接数] [并发度] [数据校验0/1] [保持秒数]

进阶用法:
    python3 load_test.py 100000 5000 1 30 \
        --pid $(pgrep -f tetris_game) \
        --json result.json \
        --min-success 0.9

说明:
    * 失败按原因分类统计（timeout / refused / reset / bind_failed / fd_exhausted ...）
    * 记录连接耗时 p50/p95/p99，并按 10 秒分桶输出建立速率曲线
    * --pid 指定服务端进程时，会周期采样 RSS / fd 数 / CPU，用于核对内存与瓶颈
    * 退出码: 0 正常; 2 成功率低于 --min-success
"""
import argparse
import asyncio
import json
import os
import socket
import sys
import time

DEFAULT_HOST = '127.0.0.1'
DEFAULT_PORT = 9999
DEFAULT_BIND_IPS = ('127.0.0.1', '127.0.0.2')


class Stats:
    def __init__(self):
        self.connected = 0
        self.failed = {}
        self.received_data = 0
        self.no_data = 0
        self.latencies = []
        self.done_times = []
        self.socks = []

    def add_failure(self, reason):
        self.failed[reason] = self.failed.get(reason, 0) + 1


class ProcSampler:
    """从 /proc 采样服务端进程的 RSS / 线程数 / fd 数 / CPU 时间。"""

    def __init__(self, pid, interval=1.0):
        self.pid = pid
        self.interval = interval
        self.samples = []
        self._task = None

    @staticmethod
    def read_rss_kb(pid):
        try:
            with open('/proc/%d/status' % pid, 'r') as f:
                for line in f:
                    if line.startswith('VmRSS:'):
                        return int(line.split()[1])
        except OSError:
            pass
        return None

    @staticmethod
    def read_threads(pid):
        try:
            with open('/proc/%d/status' % pid, 'r') as f:
                for line in f:
                    if line.startswith('Threads:'):
                        return int(line.split()[1])
        except OSError:
            pass
        return None

    @staticmethod
    def read_fd_count(pid):
        try:
            return len(os.listdir('/proc/%d/fd' % pid))
        except OSError:
            return None

    @staticmethod
    def read_cpu_ticks(pid):
        try:
            with open('/proc/%d/stat' % pid, 'r') as f:
                parts = f.read().split()
            return int(parts[13]) + int(parts[14])
        except (OSError, IndexError):
            return None

    def sample_once(self):
        sample = {
            't': time.time(),
            'rss_kb': self.read_rss_kb(self.pid),
            'threads': self.read_threads(self.pid),
            'fds': self.read_fd_count(self.pid),
            'cpu_ticks': self.read_cpu_ticks(self.pid),
        }
        self.samples.append(sample)
        return sample

    async def run(self):
        self.sample_once()
        while True:
            await asyncio.sleep(self.interval)
            self.sample_once()

    def start(self):
        self._task = asyncio.create_task(self.run())

    def stop(self):
        if self._task:
            self._task.cancel()
        self.sample_once()


def classify(exc):
    if isinstance(exc, asyncio.TimeoutError):
        return 'timeout'
    if isinstance(exc, ConnectionRefusedError):
        return 'refused'
    if isinstance(exc, ConnectionResetError):
        return 'reset'
    if isinstance(exc, OSError):
        if exc.errno == 99:      # EADDRNOTAVAIL: 本地端口/地址耗尽
            return 'bind_failed'
        if exc.errno == 24:      # EMFILE: 客户端 fd 耗尽
            return 'fd_exhausted'
        if exc.errno == 12:      # ENOMEM
            return 'out_of_memory'
        return 'os_errno_%s' % exc.errno
    return 'other:' + type(exc).__name__


async def worker(idx, bind_ip, sem, stats, read_check, connect_timeout, read_timeout):
    async with sem:
        started = time.perf_counter()
        sock = None
        try:
            # 注意：socket() 本身也可能因 fd 耗尽抛 EMFILE，必须放在 try 内
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setblocking(False)
            sock.bind((bind_ip, 0))
            loop = asyncio.get_event_loop()
            await asyncio.wait_for(loop.sock_connect(sock, (DEFAULT_HOST, DEFAULT_PORT)),
                                   timeout=connect_timeout)
            stats.connected += 1
            stats.latencies.append(time.perf_counter() - started)
            stats.done_times.append(time.perf_counter())
            stats.socks.append(sock)

            if read_check:
                try:
                    data = await asyncio.wait_for(loop.sock_recv(sock, 4096),
                                                  timeout=read_timeout)
                    if data:
                        stats.received_data += 1
                    else:
                        stats.no_data += 1
                except Exception:
                    stats.no_data += 1
            return True
        except Exception as exc:
            stats.add_failure(classify(exc))
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
            return False


def percentile(values, p):
    if not values:
        return None
    ordered = sorted(values)
    k = int(round((len(ordered) - 1) * p))
    return ordered[k]


def rate_buckets(done_times, start, bucket=10.0, t_end=None):
    """把连接完成时间按 bucket 秒分桶，返回每桶的连接数。"""
    if not done_times:
        return []
    end = t_end if t_end is not None else max(done_times)
    span = max(end - start, 1e-9)
    n = int(span // bucket) + 1
    buckets = [0] * n
    for t in done_times:
        i = int((t - start) // bucket)
        if i < 0:
            i = 0
        elif i >= n:
            i = n - 1
        buckets[i] += 1
    return buckets


def summarize(stats, target, concurrency, read_check, hold, elapsed,
              sampler, start):
    failed_total = sum(stats.failed.values())
    tried = stats.connected + failed_total
    success_rate = (stats.connected / tried) if tried else 0.0

    lat = stats.latencies
    summary = {
        'config': {
            'target': target,
            'concurrency': concurrency,
            'read_check': bool(read_check),
            'hold_seconds': hold,
            'host': DEFAULT_HOST,
            'port': DEFAULT_PORT,
        },
        'result': {
            'connected': stats.connected,
            'failed': failed_total,
            'failed_by_reason': dict(sorted(stats.failed.items())),
            'attempted': tried,
            'success_rate': round(success_rate, 6),
            'received_data': stats.received_data,
            'no_data': stats.no_data,
            'elapsed_seconds': round(elapsed, 3),
            'rate_per_second': round(stats.connected / elapsed, 1) if elapsed else 0.0,
            'latency_seconds': {
                'p50': round(percentile(lat, 0.50), 4) if lat else None,
                'p95': round(percentile(lat, 0.95), 4) if lat else None,
                'p99': round(percentile(lat, 0.99), 4) if lat else None,
                'max': round(max(lat), 4) if lat else None,
            },
            'rate_per_10s': rate_buckets(stats.done_times, start) if stats.done_times else [],
        },
        'server': {},
        'timestamp': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
    }

    if sampler and sampler.samples:
        rss = [s['rss_kb'] for s in sampler.samples if s['rss_kb']]
        fds = [s['fds'] for s in sampler.samples if s['fds']]
        ticks = [(s['t'], s['cpu_ticks']) for s in sampler.samples if s['cpu_ticks'] is not None]
        cpu_pct = None
        if len(ticks) >= 2 and ticks[-1][0] > ticks[0][0]:
            hz = os.sysconf('SC_CLK_TCK')
            cpu_pct = round(((ticks[-1][1] - ticks[0][1]) / hz) /
                            (ticks[-1][0] - ticks[0][0]) * 100.0, 1)
        summary['server'] = {
            'rss_peak_mb': round(max(rss) / 1024.0, 1) if rss else None,
            'rss_last_mb': round(rss[-1] / 1024.0, 1) if rss else None,
            'fds_peak': max(fds) if fds else None,
            'fds_last': fds[-1] if fds else None,
            'threads_last': sampler.samples[-1]['threads'],
            'cpu_percent_avg': cpu_pct,
            'samples': len(sampler.samples),
        }

    return summary, success_rate


def print_summary(s):
    cfg, res, srv = s['config'], s['result'], s['server']
    print('\n========== 压测结果 ==========')
    print('目标连接数:   %d    并发: %d    校验数据: %s'
          % (cfg['target'], cfg['concurrency'], '开' if cfg['read_check'] else '关'))
    print('成功建立:     %d (%.2f%%)' % (res['connected'], res['success_rate'] * 100))
    print('失败:         %d' % res['failed'])
    if res['failed_by_reason']:
        print('失败原因:     ' + ', '.join('%s=%d' % kv for kv in res['failed_by_reason'].items()))
    if cfg['read_check']:
        print('收到欢迎数据: %d' % res['received_data'])
    lat = res['latency_seconds']
    if lat['p50'] is not None:
        print('连接耗时:     p50=%.3fs  p95=%.3fs  p99=%.3fs  max=%.3fs'
              % (lat['p50'], lat['p95'], lat['p99'], lat['max']))
    print('总耗时:       %.2f 秒' % res['elapsed_seconds'])
    print('建立速率:     %.0f 连接/秒' % res['rate_per_second'])
    if srv.get('rss_peak_mb') is not None:
        print('服务端内存:   峰值 %.1f MB / 末值 %.1f MB'
              % (srv['rss_peak_mb'], srv['rss_last_mb']))
    if srv.get('fds_peak') is not None:
        print('服务端 fd:    峰值 %d' % srv['fds_peak'])
    if srv.get('cpu_percent_avg') is not None:
        print('服务端 CPU:   平均 %.1f%%' % srv['cpu_percent_avg'])
    buckets = res['rate_per_10s']
    if len(buckets) > 1:
        shown = buckets[:12]
        print('每10秒建立:   ' + ', '.join(str(x) for x in shown) +
              (' ...' if len(buckets) > len(shown) else ''))


async def main(args):
    stats = Stats()
    sem = asyncio.Semaphore(args.concurrency)

    sampler = ProcSampler(args.pid, args.sample_interval) if args.pid else None
    if sampler:
        sampler.start()

    print('开始压测: 目标 %d 连接, 并发 %d, 校验数据=%s, 超时 %.0fs'
          % (args.target, args.concurrency, '开' if args.read_check else '关',
             args.connect_timeout))
    start = time.perf_counter()
    tasks = []
    for i in range(args.target):
        bind_ip = args.bind_ips[i % len(args.bind_ips)]
        tasks.append(asyncio.create_task(
            worker(i, bind_ip, sem, stats, args.read_check,
                   args.connect_timeout, args.read_timeout)))
    await asyncio.gather(*tasks)
    elapsed = time.perf_counter() - start

    if sampler:
        sampler.stop()
    summary, success_rate = summarize(stats, args.target, args.concurrency,
                                      args.read_check, args.hold, elapsed,
                                      sampler, start)
    print_summary(summary)

    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
        print('结果已写入:   %s' % args.json)

    if args.hold > 0:
        print('\n保持 %d 秒验证同时在线稳定性...' % args.hold)
        await asyncio.sleep(args.hold)
        print('保持结束，活跃连接: %d' % len(stats.socks))

    for s in stats.socks:
        try:
            s.close()
        except OSError:
            pass
    print('已关闭所有连接，压测结束')

    if args.min_success and success_rate < args.min_success:
        print('成功率 %.2f%% 低于阈值 %.2f%%'
              % (success_rate * 100, args.min_success * 100))
        return 2
    return 0


def parse_args(argv):
    p = argparse.ArgumentParser(description='Russia-Tetris 并发压测客户端')
    p.add_argument('target', nargs='?', type=int, default=100000, help='目标连接数（默认 100000）')
    p.add_argument('concurrency', nargs='?', type=int, default=5000, help='并发度（默认 5000）')
    p.add_argument('read_check', nargs='?', type=int, default=1, help='是否读取欢迎数据 0/1（默认 1）')
    p.add_argument('hold', nargs='?', type=int, default=5, help='保持连接秒数（默认 5）')
    p.add_argument('--pid', type=int, default=0, help='服务端进程 pid，用于采样 RSS/fd/CPU')
    p.add_argument('--sample-interval', type=float, default=1.0, help='采样间隔秒（默认 1.0）')
    p.add_argument('--json', default='', help='把结果写成 JSON 文件的路径')
    p.add_argument('--connect-timeout', type=float, default=10.0, help='单连接超时秒（默认 10）')
    p.add_argument('--read-timeout', type=float, default=5.0, help='读取欢迎数据超时秒（默认 5）')
    p.add_argument('--min-success', type=float, default=0.0, help='成功率低于该值时退出码为 2')
    p.add_argument('--bind-ips', default=','.join(DEFAULT_BIND_IPS),
                   help='客户端源 IP 列表，逗号分隔（默认 127.0.0.1,127.0.0.2）')
    args = p.parse_args(argv)
    args.bind_ips = [ip for ip in args.bind_ips.split(',') if ip]
    if not args.bind_ips:
        p.error('--bind-ips 不能为空')
    return args


if __name__ == '__main__':
    parsed = parse_args(sys.argv[1:])
    try:
        sys.exit(asyncio.run(main(parsed)))
    except KeyboardInterrupt:
        print('\n已中断')
        sys.exit(130)
