#!/usr/bin/env bash
# 一键并发压测：环境检查 -> 构建 -> 启动服务端 -> 采样 -> 压测 -> 生成报告
#
# 用法:
#   bash tests/bench.sh [目标连接数] [并发度] [保持秒数] [是否读取欢迎数据0/1] [客户端进程数]
#   bash tests/bench.sh 100000 5000 30 1 2
#
# 说明:
#   * 会尝试把 ulimit -n 提到 40 万，并在有 root/sudo 时调整内核参数
#   * 服务端在 src/ 下启动，userdata.csv / log.txt 自动生成（已 gitignore）
#   * 结果写入 bench-results/<时间戳>/，包含 result.json、result.txt、server.log、summary.md
#   * 客户端进程数 > 1 时按目标数拆分，各进程绑定 127.0.0.1、127.0.0.2 ... 并合并结果
set -euo pipefail

TARGET=${1:-100000}
CONCURRENCY=${2:-5000}
HOLD=${3:-30}
READ_CHECK=${4:-1}
CLIENTS=${5:-1}

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SRC="$ROOT/src"
TESTS="$ROOT/tests"
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${OUT_DIR:-"$ROOT/bench-results/$STAMP"}
mkdir -p "$OUT"

log() { printf '\n\033[1m[%s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }

for cmd in g++ make python3; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "缺少命令: $cmd"; exit 1; }
done

log "环境信息"
{
  echo "时间      : $(date -Is)"
  echo "内核      : $(uname -srm)"
  echo "CPU       : $(nproc) 核"
  if command -v free >/dev/null 2>&1; then
    echo "内存      : $(free -h | awk '/Mem:/{print $2}')"
  fi
  echo "参数      : target=$TARGET concurrency=$CONCURRENCY hold=$HOLD read_check=$READ_CHECK clients=$CLIENTS"
} | tee "$OUT/env.txt"

log "调整文件描述符上限"
if ! ulimit -n 400000 2>/dev/null; then
  echo "直接提升失败（硬上限 $(ulimit -Hn)），尝试 sudo prlimit" | tee -a "$OUT/env.txt"
  if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo prlimit --pid $$ --nofile=400000:400000 2>/dev/null || true
  fi
fi
echo "ulimit -n = $(ulimit -n)（硬上限 $(ulimit -Hn)）" | tee -a "$OUT/env.txt"

log "调整内核参数（需要 root/sudo，失败则跳过）"
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi
if [ "$(id -u)" -eq 0 ] || sudo -n true 2>/dev/null; then
  $SUDO sysctl -w net.core.somaxconn=65535 >/dev/null 2>&1 || true
  $SUDO sysctl -w net.ipv4.tcp_max_syn_backlog=65535 >/dev/null 2>&1 || true
  $SUDO sysctl -w net.ipv4.ip_local_port_range="1024 65535" >/dev/null 2>&1 || true
  $SUDO sysctl -w net.ipv4.tcp_tw_reuse=1 >/dev/null 2>&1 || true
  echo "内核参数已尝试调整（详见系统 sysctl）" | tee -a "$OUT/env.txt"
else
  echo "无 sudo 权限，跳过内核参数调整（可能影响大连接数结果）" | tee -a "$OUT/env.txt"
fi

log "构建"
if ! make -C "$SRC" -j"$(nproc)" > "$OUT/build.log" 2>&1; then
  echo "构建失败，最后 40 行日志："
  tail -40 "$OUT/build.log"
  exit 1
fi
echo "构建完成: $SRC/tetris_game" | tee -a "$OUT/env.txt"

log "启动服务端"
cd "$SRC"
START_TS=$(date +%s.%N)
./tetris_game > "$OUT/server.log" 2>&1 &
SRV_PID=$!
cleanup() {
  kill "$SRV_PID" 2>/dev/null || true
  wait "$SRV_PID" 2>/dev/null || true
}
trap cleanup EXIT

READY=0
for _ in $(seq 1 180); do
  if ! kill -0 "$SRV_PID" 2>/dev/null; then
    echo "服务端启动后退出，日志："
    tail -40 "$OUT/server.log"
    exit 1
  fi
  if (exec 3<>/dev/tcp/127.0.0.1/9999) 2>/dev/null; then
    READY=1
    break
  fi
  sleep 1
done
if [ "$READY" -ne 1 ]; then
  echo "等待 180 秒端口仍未就绪，日志："
  tail -40 "$OUT/server.log"
  exit 1
fi
STARTUP=$(python3 -c 'import sys,time; print("%.1f" % (time.time() - float(sys.argv[1])))' "$START_TS")
echo "服务端就绪，启动耗时 ${STARTUP}s (pid=$SRV_PID)" | tee -a "$OUT/env.txt"

log "开始压测 (客户端进程数: $CLIENTS)"
set +e
if [ "$CLIENTS" -le 1 ]; then
  python3 "$TESTS/load_test.py" "$TARGET" "$CONCURRENCY" "$READ_CHECK" "$HOLD" \
    --pid "$SRV_PID" \
    --sample-interval 1 \
    --json "$OUT/result.json" \
    2>&1 | tee "$OUT/result.txt"
  RC=${PIPESTATUS[0]}
else
  PER=$((TARGET / CLIENTS))
  REM=$((TARGET % CLIENTS))
  CONC=$((CONCURRENCY / CLIENTS))
  [ "$CONC" -lt 1 ] && CONC=1
  SHARD_PIDS=()
  for i in $(seq 0 $((CLIENTS - 1))); do
    T=$PER
    if [ "$i" -lt "$REM" ]; then T=$((PER + 1)); fi
    IP="127.0.0.$((i + 1))"
    EXTRA=""
    if [ "$i" -eq 0 ]; then EXTRA="--pid $SRV_PID"; fi
    python3 "$TESTS/load_test.py" "$T" "$CONC" "$READ_CHECK" "$HOLD" \
      --bind-ips "$IP" --json "$OUT/result-$i.json" $EXTRA \
      2>&1 | tee "$OUT/result-$i.txt" &
    SHARD_PIDS+=("$!")
  done
  RC=0
  for p in "${SHARD_PIDS[@]}"; do
    wait "$p" || RC=1
  done
  python3 - "$OUT" "$CLIENTS" <<'PY' | tee "$OUT/result.txt"
import json
import os
import sys

out, n = sys.argv[1], int(sys.argv[2])
shards = []
for i in range(n):
    p = os.path.join(out, 'result-%d.json' % i)
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            shards.append(json.load(f))
if not shards:
    print('没有可合并的分片结果')
    sys.exit(0)

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

merged = {
    'config': {
        'target': sum(s['config']['target'] for s in shards),
        'concurrency': sum(s['config']['concurrency'] for s in shards),
        'read_check': first['config']['read_check'],
        'hold_seconds': first['config']['hold_seconds'],
        'host': first['config']['host'],
        'port': first['config']['port'],
        'clients': n,
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
        'latency_seconds': {'p50': None, 'p95': None, 'p99': None, 'max': None},
        'rate_per_10s': curve,
    },
    'server': first.get('server', {}),
    'timestamp': first.get('timestamp'),
}
with open(os.path.join(out, 'result.json'), 'w', encoding='utf-8') as f:
    json.dump(merged, f, ensure_ascii=False, indent=2)
print('合并 %d 个分片: 成功 %d / 失败 %d, 耗时 %.1fs, %.0f 连接/秒'
      % (n, connected, failed, elapsed, connected / elapsed if elapsed else 0))
PY
fi
set -e
echo "压测退出码: $RC" | tee -a "$OUT/result.txt"

log "生成报告"
python3 - "$OUT/result.json" "$OUT/summary.md" "$STARTUP" "$OUT/env.txt" <<'PY'
import json
import os
import sys

result_path, summary_path, startup, env_path = sys.argv[1:5]
summary = {}
if os.path.exists(result_path):
    with open(result_path, encoding='utf-8') as f:
        summary = json.load(f)

lines = ['# 压测报告', '', '## 环境', '', '```']
with open(env_path, encoding='utf-8') as f:
    lines += [line.rstrip('\n') for line in f]
lines += ['```', '']

if summary:
    cfg, res, srv = summary['config'], summary['result'], summary.get('server', {})
    lines += ['## 结果', '', '| 指标 | 值 |', '| --- | --- |']
    lines.append('| 目标连接数 | %s |' % cfg['target'])
    lines.append('| 并发度 | %s |' % cfg['concurrency'])
    lines.append('| 成功建立 | %s（%.2f%%） |' % (res['connected'], res['success_rate'] * 100))
    lines.append('| 失败 | %s |' % res['failed'])
    if res['failed_by_reason']:
        lines.append('| 失败原因 | %s |' %
                     ', '.join('%s=%s' % kv for kv in res['failed_by_reason'].items()))
    lines.append('| 收到欢迎数据 | %s |' % res['received_data'])
    lat = res['latency_seconds']
    if lat.get('p50') is not None:
        lines.append('| 连接耗时 | p50=%.3fs, p95=%.3fs, p99=%.3fs |' %
                     (lat['p50'], lat['p95'], lat['p99']))
    lines.append('| 总耗时 | %.2f 秒 |' % res['elapsed_seconds'])
    lines.append('| 建立速率 | %.0f 连接/秒 |' % res['rate_per_second'])
    if srv.get('rss_peak_mb') is not None:
        lines.append('| 服务端内存 | 峰值 %.1f MB / 末值 %.1f MB |' %
                     (srv['rss_peak_mb'], srv['rss_last_mb']))
    if srv.get('fds_peak') is not None:
        lines.append('| 服务端 fd | 峰值 %s |' % srv['fds_peak'])
    if srv.get('cpu_percent_avg') is not None:
        lines.append('| 服务端 CPU | 平均 %.1f%% |' % srv['cpu_percent_avg'])
    lines.append('| 服务端启动耗时 | %ss |' % startup)
    lines.append('')
    buckets = res.get('rate_per_10s') or []
    if len(buckets) > 1:
        lines += ['## 每 10 秒建立速率', '', '```', ', '.join(str(x) for x in buckets), '```', '']
else:
    lines += ['## 结果', '', '未生成 result.json，请查看 result.txt / server.log', '']

text = '\n'.join(lines)
with open(summary_path, 'w', encoding='utf-8') as f:
    f.write(text + '\n')
print(text)
PY

if [ -n "${GITHUB_STEP_SUMMARY:-}" ] && [ -f "$OUT/summary.md" ]; then
  cat "$OUT/summary.md" >> "$GITHUB_STEP_SUMMARY"
fi

log "完成，产物目录: $OUT"
ls -la "$OUT"

exit "$RC"
