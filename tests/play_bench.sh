#!/usr/bin/env bash
# 游戏负载多 worker 对比：每个连接真实登录、开局、持续按键，
# 用「服务端 CPU」和「客户端收到的字节数」衡量实际完成的工作量。
#
# 用法:
#   bash tests/play_bench.sh [玩家数] [时长秒] [按键间隔] [workers列表] [端口]
#   bash tests/play_bench.sh 200 15 0.1 "1 2 4" 10009
#
# 每组结果在 stdout 打印一行，并以 ::notice:: 注解回传（CI 里日志读不到，
# 注解是唯一能看到结果的通道）。
set -u

PLAYERS=${1:-200}
DURATION=${2:-15}
KEY_DELAY=${3:-0.1}
WORKERS_LIST=${4:-"1 2 4"}
PORT=${5:-10009}

TESTS="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$TESTS/../src" && pwd)"
OUT="play-results/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

cd "$SRC" || exit 1
echo "构建中..."
if ! make -j"$(nproc)" > "$OUT/build.log" 2>&1; then
  echo "构建失败："
  tail -30 "$OUT/build.log"
  exit 1
fi

for w in $WORKERS_LIST; do
  rm -f userdata.csv userdata.csv.lock log*.txt

  # 先造账号，省掉 N 次注册往返，把时间留给真正的游戏负载
  python3 "$TESTS/play_test.py" --seed "$PLAYERS" --userdata userdata.csv > "$OUT/seed-$w.log" 2>&1

  RP=0
  if [ "$w" -gt 1 ]; then RP=1; fi

  TETRIS_WORKERS=$w TETRIS_REUSEPORT=$RP TETRIS_PORT=$PORT \
    ./tetris_game > "$OUT/server-$w.log" 2>&1 &
  srv=$!

  ready=0
  for _ in $(seq 1 60); do
    if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then
      ready=1
      break
    fi
    sleep 1
  done
  if [ "$ready" -ne 1 ]; then
    echo "::warning::workers=$w 服务端未能就绪"
    tail -20 "$OUT/server-$w.log"
    kill "$srv" 2>/dev/null
    wait "$srv" 2>/dev/null
    continue
  fi

  python3 "$TESTS/sample_proc.py" "$srv" "$OUT/samples-$w.json" 1 &
  sampler=$!

  python3 "$TESTS/play_test.py" --players "$PLAYERS" --seconds "$DURATION" \
    --key-delay "$KEY_DELAY" --port "$PORT" --json "$OUT/play-$w.json"
  rc=$?

  kill "$sampler" 2>/dev/null
  wait "$sampler" 2>/dev/null
  kill "$srv" 2>/dev/null
  wait "$srv" 2>/dev/null
  sleep 1

  python3 - "$OUT/play-$w.json" "$OUT/samples-$w.json" "$w" "$rc" <<'PY'
import json
import os
import sys

play_path, samp_path, workers, rc = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

play = {}
try:
    with open(play_path, encoding='utf-8') as f:
        play = json.load(f)
except OSError:
    pass

cpu = rss = fds = procs = None
try:
    with open(samp_path, encoding='utf-8') as f:
        samples = [s for s in json.load(f).get('samples', []) if s]
    ticks = [(s['t'], s.get('cpu_ticks')) for s in samples if s.get('cpu_ticks') is not None]
    if len(ticks) >= 2 and ticks[-1][0] > ticks[0][0]:
        hz = os.sysconf('SC_CLK_TCK')
        cpu = round((ticks[-1][1] - ticks[0][1]) / hz / (ticks[-1][0] - ticks[0][0]) * 100.0, 1)
    rss_values = [s.get('rss_kb') or 0 for s in samples]
    fd_values = [s.get('fds') or 0 for s in samples]
    if rss_values:
        rss = round(max(rss_values) / 1024.0, 1)
    if fd_values:
        fds = max(fd_values)
    if samples:
        procs = samples[-1].get('procs')
except OSError:
    pass

print("::notice::[workers=%s] 玩家 %s/%s 就绪, 按键 %s 次（%s/s）, 下行 %s KB/s, "
      "服务端 CPU %s%%（占满一核=100%%）, RSS %s MB, fd %s, 进程数 %s, 退出码 %s"
      % (workers,
         play.get('ready', '?'), play.get('players', '?'),
         play.get('keys', '?'), play.get('keys_per_second', '?'),
         play.get('rx_kb_per_second', '?'),
         cpu, rss, fds, procs, rc))
PY
done
