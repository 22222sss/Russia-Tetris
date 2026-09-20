#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
俄罗斯方块服务端并发压测客户端
用法: python3 load_test.py [目标连接数] [并发度]
默认: 目标 100000, 并发 5000
"""
import asyncio
import socket
import sys
import time

HOST = '127.0.0.1'
PORT = 9999

# 统计结果
class Stats:
    def __init__(self):
        self.connected = 0
        self.failed = 0
        self.received_data = 0   # 收到服务端欢迎数据的连接数
        self.socks = []          # 保持连接，防止被 GC 关闭


async def worker(idx, bind_ip, sem, stats, read_check):
    """单个连接的建立与保持"""
    async with sem:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setblocking(False)
        try:
            sock.bind((bind_ip, 0))
            loop = asyncio.get_event_loop()
            # 非阻塞 connect，10 秒超时
            await asyncio.wait_for(loop.sock_connect(sock, (HOST, PORT)), timeout=10)
            stats.connected += 1
            stats.socks.append(sock)

            if read_check:
                # 尝试读取服务端下发的欢迎界面数据，验证数据收发正常
                try:
                    data = await asyncio.wait_for(loop.sock_recv(sock, 4096), timeout=5)
                    if data:
                        stats.received_data += 1
                except Exception:
                    pass
            return True
        except Exception:
            stats.failed += 1
            try:
                sock.close()
            except Exception:
                pass
            return False


async def main(target, concurrency, read_check, hold=5):
    stats = Stats()
    sem = asyncio.Semaphore(concurrency)
    half = target // 2

    tasks = []
    for i in range(target):
        # 两个源 IP 各承担一半连接，突破单 IP 端口上限
        bind_ip = '127.0.0.1' if i < half else '127.0.0.2'
        tasks.append(asyncio.create_task(worker(i, bind_ip, sem, stats, read_check)))

    print(f"开始压测: 目标 {target} 连接, 并发 {concurrency}, 数据校验={'开' if read_check else '关'}")
    start = time.time()
    await asyncio.gather(*tasks)
    elapsed = time.time() - start

    print(f"\n========== 压测结果 ==========")
    print(f"目标连接数:   {target}")
    print(f"成功建立:     {stats.connected}")
    print(f"失败:         {stats.failed}")
    if read_check:
        print(f"收到欢迎数据: {stats.received_data}")
    print(f"总耗时:       {elapsed:.2f} 秒")
    print(f"建立速率:     {stats.connected / elapsed:.0f} 连接/秒")
    print(f"当前活跃连接: {len(stats.socks)}")

    # 保持连接存活，验证"同时在线"能力
    print(f"\n保持 {hold} 秒验证同时在线稳定性...")
    await asyncio.sleep(hold)
    print(f"保持结束，活跃连接: {len(stats.socks)}")

    # 关闭所有连接
    for s in stats.socks:
        try:
            s.close()
        except Exception:
            pass
    print("已关闭所有连接，压测结束")


if __name__ == '__main__':
    target = int(sys.argv[1]) if len(sys.argv) > 1 else 100000
    concurrency = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
    read_check = bool(int(sys.argv[3])) if len(sys.argv) > 3 else True
    hold = int(sys.argv[4]) if len(sys.argv) > 4 else 5
    asyncio.run(main(target, concurrency, read_check, hold))
