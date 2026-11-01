#!/usr/bin/env bash
# 游戏负载多 worker 对比：每个连接真实登录、开局、持续按键，
# 用「服务端 CPU」和「客户端收到的字节数」衡量实际完成的工作量。
#
# 用法:
#   bash tests/play_bench.sh [玩家数] [时长秒] [按键间隔] [workers列表] [端口]
#   bash tests/play_bench.sh 200 15 0.1 "1 2 4" 10009
#
# 可选环境变量（做"同机 A/B"用：不同 runner 之间的波动可达 ±40%）：
#   BENCH_BIN=/abs/path/tetris_game_O2   直接用这个二进制，跳过 make
#   BENCH_LABEL="workers=1 opt=O2"       结果标签（默认 workers=N）
#
# 每组结果在 stdout 打印一行，并以 ::notice:: 注解回传（CI 里日志读不到，
# 注解是唯一能看到结果的通道）。CPU 同时给"游戏窗口"和"全程"两个口径，
# 每按键 µs 见 tests/gameplay_stats.py 的说明。
set -u

PLAYERS=${1:-200}
DURATION=${2:-15}
KEY_DELAY=${3:-0.1}
WORKERS_LIST=${4:-"1 2 4"}
PORT=${5:-10009}

TESTS="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$TESTS/../src" && pwd)"
# 结果目录必须用绝对路径：后面会 cd 进 src/，相对路径会指到别处去
OUT="$(pwd)/play-results/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

cd "$SRC" || exit 1
BIN=${BENCH_BIN:-}
if [ -z "$BIN" ]; then
  BIN="$SRC/tetris_game"
  echo "构建中..."
  if ! make -j"$(nproc)" > "$OUT/build.log" 2>&1; then
    echo "构建失败："
    tail -30 "$OUT/build.log"
    exit 1
  fi
  echo "优化级别: $(make -s print-opt 2>/dev/null || echo '?')"
else
  # 同机 A/B 时由调用方预先编好二进制，标签里自己写清是哪个级别
  echo "使用预编译二进制（跳过 make）: $BIN"
fi
if [ ! -x "$BIN" ]; then
  echo "::error::服务端二进制不存在或不可执行: $BIN"
  exit 1
fi

for w in $WORKERS_LIST; do
  LABEL=${BENCH_LABEL:-workers=$w}
  # 文件名用标签的"安全"形式：标签里可能有空格/等号
  SAFE=$(printf '%s' "$LABEL" | tr -c 'A-Za-z0-9._-' '_')
  rm -f userdata.csv userdata.csv.lock log*.txt

  # 先造账号，省掉 N 次注册往返，把时间留给真正的游戏负载
  python3 "$TESTS/play_test.py" --seed "$PLAYERS" --userdata userdata.csv > "$OUT/seed-$SAFE.log" 2>&1

  RP=0
  if [ "$w" -gt 1 ]; then RP=1; fi

  TETRIS_WORKERS=$w TETRIS_REUSEPORT=$RP TETRIS_PORT=$PORT \
    "$BIN" > "$OUT/server-$SAFE.log" 2>&1 &
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
    echo "::warning::$LABEL 服务端未能就绪"
    tail -20 "$OUT/server-$SAFE.log"
    kill "$srv" 2>/dev/null
    wait "$srv" 2>/dev/null
    continue
  fi

  python3 "$TESTS/sample_proc.py" "$srv" "$OUT/samples-$SAFE.json" 1 &
  sampler=$!

  python3 "$TESTS/play_test.py" --players "$PLAYERS" --seconds "$DURATION" \
    --key-delay "$KEY_DELAY" --port "$PORT" --json "$OUT/play-$SAFE.json"
  rc=$?

  kill "$sampler" 2>/dev/null
  wait "$sampler" 2>/dev/null
  kill "$srv" 2>/dev/null
  wait "$srv" 2>/dev/null
  sleep 1

  python3 "$TESTS/gameplay_stats.py" \
    "$OUT/play-$SAFE.json" "$OUT/samples-$SAFE.json" "$LABEL" "$rc"
done
