#!/usr/bin/env bash
# 登录吞吐基准：造哈希账号 -> 起服务端 -> N 个连接并发真实登录 -> 回传注解。
#
# 用法:
#   bash tests/login_bench.sh [玩家数] [workers列表] [端口] [每组轮数]
#   bash tests/login_bench.sh 200 "1" 10099 3
#   bash tests/login_bench.sh 200 "1 2 4" 10099 2
#
# 只测登录阶段（不开始游戏）：密码校验是 PBKDF2，跑在异步文件线程上串行执行，
# 所以这里量的是"登录高峰"的吞吐，不是游戏负载（游戏负载用 play_bench.sh）。
set -u

PLAYERS=${1:-200}
WORKERS_LIST=${2:-"1"}
PORT=${3:-10099}
TRIALS=${4:-1}

TESTS="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$TESTS/../src" && pwd)"
# 结果目录必须在 cd 之前算成绝对路径（见 HANDOVER 6.12）
OUT="$(pwd)/login-results/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

cd "$SRC" || exit 1
if [ ! -x ./tetris_game ]; then
  echo "::error::src/tetris_game 不存在，请先在 src/ 下 make"
  exit 1
fi

rm -f userdata.csv userdata.csv.lock log*.txt
echo "造账号中（$PLAYERS 个，每个账号一次 PBKDF2，需要几十秒）..."
if ! python3 "$TESTS/login_bench.py" --seed "$PLAYERS" --userdata userdata.csv; then
  echo "::error::登录基准造账号失败"
  exit 1
fi

failed=0
for w in $WORKERS_LIST; do
  for t in $(seq 1 "$TRIALS"); do
    rm -f log*.txt
    TETRIS_WORKERS=$w TETRIS_PORT=$PORT ./tetris_game > "$OUT/server-w$w-t$t.log" 2>&1 &
    srv=$!

    ready=0
    for _ in $(seq 1 120); do
      if (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null; then
        ready=1
        break
      fi
      sleep 0.5
    done

    if [ "$ready" -ne 1 ]; then
      echo "::error::登录基准: workers=$w 第 $t 轮服务端未就绪"
      tail -20 "$OUT/server-w$w-t$t.log"
      kill "$srv" 2>/dev/null
      wait "$srv" 2>/dev/null
      failed=1
      continue
    fi

    python3 "$TESTS/login_bench.py" --players "$PLAYERS" --port "$PORT" \
      --label "workers=$w 第$t轮" --json "$OUT/bench-w$w-t$t.json"
    rc=$?

    kill "$srv" 2>/dev/null
    wait "$srv" 2>/dev/null
    sleep 1

    if [ "$rc" -ne 0 ]; then
      echo "::error::登录基准: workers=$w 第 $t 轮失败（rc=$rc）"
      failed=1
    fi
  done
done

if [ "$failed" -ne 0 ]; then
  exit 1
fi

echo "结果目录: $OUT"
