#!/usr/bin/env bash
# 一键并发压测：环境检查 -> 构建 -> 启动服务端 -> 采样 -> 压测 -> 生成报告
#
# 本地模式（客户端与服务端同机）:
#   bash tests/bench.sh [目标连接数] [并发度] [保持秒数] [是否读取欢迎数据0/1] [客户端进程数]
#   bash tests/bench.sh 200000 10000 30 1 4
#
# 远程客户端模式（服务端在本机，客户端在别的机器，需要免密 ssh）:
#   BENCH_REMOTE_CLIENTS="user@host1,user@host2" BENCH_HOST=10.0.0.5 \
#       bash tests/bench.sh 200000 10000 30 1 4
#   * BENCH_HOST 是远程客户端要连接的服务端地址；不填则自动探测本机第一个 IP
#   * 第 1~N 个分片交给 BENCH_REMOTE_CLIENTS 里的机器，剩余分片在本机跑
#   * 远程机器只需要 python3，脚本会通过 ssh 自动上传到 /tmp/russia-bench
#
# 说明:
#   * 会尝试把 ulimit -n 提到 40 万，并在有 root/sudo 时调整内核参数
#   * 服务端在 src/ 下启动，userdata.csv / log.txt 自动生成（已 gitignore）
#   * 结果写入 bench-results/<时间戳>/，包含 result.json、result.txt、server.log、summary.md
set -euo pipefail

TARGET=${1:-100000}
CONCURRENCY=${2:-5000}
HOLD=${3:-30}
READ_CHECK=${4:-1}
CLIENTS=${5:-1}

REMOTE_CLIENTS=${BENCH_REMOTE_CLIENTS:-}
# worker 进程数（0/1 = 单进程）。多 worker 时服务端是"父进程 + N 个子进程"，
# 所以采样必须按进程树统计，否则 CPU/RSS 会全读到 0。
WORKERS=${BENCH_WORKERS:-1}
HOST=${BENCH_HOST:-}
REMOTE_DIR=/tmp/russia-bench
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)

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
if [ "$CLIENTS" -lt 1 ]; then CLIENTS=1; fi

REMOTE_HOSTS=()
if [ -n "$REMOTE_CLIENTS" ]; then
  command -v ssh >/dev/null 2>&1 || { echo "远程客户端模式需要 ssh 命令"; exit 1; }
  IFS=',' read -r -a REMOTE_HOSTS <<< "$REMOTE_CLIENTS"
fi
REMOTE_COUNT=${#REMOTE_HOSTS[@]}

if [ "$REMOTE_COUNT" -gt 0 ] && [ -z "$HOST" ]; then
  HOST=$(hostname -I 2>/dev/null | awk '{print $1}')
  if [ -z "$HOST" ]; then
    HOST=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}')
  fi
  if [ -z "$HOST" ]; then
    echo "无法自动探测本机 IP，请用 BENCH_HOST 指定远程客户端要连接的服务端地址"
    exit 1
  fi
fi
HOST=${HOST:-127.0.0.1}

log "环境信息"
{
  echo "时间      : $(date -Is)"
  echo "内核      : $(uname -srm)"
  echo "CPU       : $(nproc) 核"
  if command -v free >/dev/null 2>&1; then
    echo "内存      : $(free -h | awk '/Mem:/{print $2}')"
  fi
  echo "参数      : target=$TARGET concurrency=$CONCURRENCY hold=$HOLD read_check=$READ_CHECK clients=$CLIENTS"
  echo "服务端地址: $HOST:9999"
  if [ "$REMOTE_COUNT" -gt 0 ]; then
    echo "远程客户端: $REMOTE_CLIENTS"
  else
    echo "远程客户端: 无（本机压测）"
  fi
} | tee "$OUT/env.txt"

log "准备远程客户端"
if [ "$REMOTE_COUNT" -gt 0 ]; then
  for h in "${REMOTE_HOSTS[@]}"; do
    if ! ssh "${SSH_OPTS[@]}" "$h" "mkdir -p $REMOTE_DIR && python3 --version" >/dev/null 2>&1; then
      echo "无法通过 ssh 连接 $h（需要免密登录且远端有 python3）"
      exit 1
    fi
    if ! ssh "${SSH_OPTS[@]}" "$h" "cat > $REMOTE_DIR/load_test.py" < "$TESTS/load_test.py"; then
      echo "上传压测脚本到 $h 失败"
      exit 1
    fi
    echo "已准备远程客户端: $h" | tee -a "$OUT/env.txt"
  done
else
  echo "未配置 BENCH_REMOTE_CLIENTS，使用本机压测" | tee -a "$OUT/env.txt"
fi

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
echo "workers   : $WORKERS" | tee -a "$OUT/env.txt"

log "启动服务端"
cd "$SRC"
START_TS=$(date +%s.%N)
TETRIS_WORKERS=$WORKERS ./tetris_game > "$OUT/server.log" 2>&1 &
SRV_PID=$!
SAMPLER_PID=""
cleanup() {
  if [ -n "$SAMPLER_PID" ]; then
    kill "$SAMPLER_PID" 2>/dev/null || true
  fi
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

log "开始采样服务端资源"
python3 "$TESTS/sample_proc.py" "$SRV_PID" "$OUT/server_samples.json" 1 &
SAMPLER_PID=$!

log "开始压测 (客户端进程数: $CLIENTS, 远程: $REMOTE_COUNT)"
PER=$((TARGET / CLIENTS))
REM=$((TARGET % CLIENTS))
CONC=$((CONCURRENCY / CLIENTS))
[ "$CONC" -lt 1 ] && CONC=1

set +e
PIDS=()
for i in $(seq 0 $((CLIENTS - 1))); do
  T=$PER
  if [ "$i" -lt "$REM" ]; then T=$((PER + 1)); fi
  if [ "$i" -lt "$REMOTE_COUNT" ]; then
    h=${REMOTE_HOSTS[$i]}
    ssh "${SSH_OPTS[@]}" "$h" \
      "bash -c 'ulimit -n 400000 2>/dev/null; python3 $REMOTE_DIR/load_test.py $T $CONC $READ_CHECK $HOLD --host $HOST --bind-ips 127.0.0.1 --json $REMOTE_DIR/result-$i.json'" \
      > "$OUT/remote-$i.log" 2>&1 &
    PIDS+=("$!")
  else
    LOCAL_IDX=$((i - REMOTE_COUNT))
    IP="127.0.0.$((LOCAL_IDX + 1))"
    (
      python3 "$TESTS/load_test.py" "$T" "$CONC" "$READ_CHECK" "$HOLD" \
        --host 127.0.0.1 --bind-ips "$IP" --json "$OUT/result-$i.json" \
        2>&1 | tee "$OUT/result-$i.txt"
      exit "${PIPESTATUS[0]}"
    ) &
    PIDS+=("$!")
  fi
done
RC=0
for p in "${PIDS[@]}"; do
  wait "$p" || RC=1
done

log "回收远程结果"
for i in $(seq 0 $((CLIENTS - 1))); do
  if [ "$i" -lt "$REMOTE_COUNT" ]; then
    h=${REMOTE_HOSTS[$i]}
    if ! ssh "${SSH_OPTS[@]}" "$h" "cat $REMOTE_DIR/result-$i.json" > "$OUT/result-$i.json" 2>/dev/null; then
      echo "分片 $i 的结果回收失败（$h）"
      rm -f "$OUT/result-$i.json"
      RC=1
    fi
  fi
done

kill -TERM "$SAMPLER_PID" 2>/dev/null || true
wait "$SAMPLER_PID" 2>/dev/null || true
SAMPLER_PID=""

log "合并结果"
python3 "$TESTS/merge_results.py" "$OUT" "$CLIENTS" | tee "$OUT/result.txt"
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
    lines.append('| 客户端进程数 | %s |' % cfg.get('clients', 1))
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
