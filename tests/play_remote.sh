#!/usr/bin/env bash
# 远程游戏负载压测：客户端在本地，服务端在一台独立机器（公网 VPS）。
#
# 为什么需要：同机压测时压测端和服务端抢同一批 CPU，多 worker 的收益会被掩盖
# （见 docs/HANDOVER.md 4.3）。本脚本假设 VPS 上已经 clone + 编译好仓库。
#
# 用法:
#   bash tests/play_remote.sh --ssh root@1.2.3.4 --players 800 --workers "1 2 4"
#   bash tests/play_remote.sh --ssh root@1.2.3.4 --host 1.2.3.4 --port 9999 \
#       --players 1600 --seconds 30 --key-delay 0.02 --workers "1 2 4" --rounds 2
#
# 轮次顺序：第 1 轮升序、第 2 轮降序（逆序复跑，抵消顺序效应）。
#
# 前置（VPS，Ubuntu 22.04/24.04）:
#   本地到 VPS 需要免密 ssh（脚本用 BatchMode，不会交互问密码）
#   sudo apt install -y g++ make libevent-dev libspdlog-dev libfmt-dev libssl-dev python3
#   git clone https://github.com/22222sss/Russia-Tetris.git
#   cd Russia-Tetris/src && make -j"$(nproc)"
#   安全组放行 TCP $PORT（默认 9999）
#
# 注意:
#   * 脚本会清空 VPS 上该仓库的 userdata.csv，并结束该机器上所有 tetris_game 进程；
#   * 只用于专用压测机，不要指向有别的业务的机器；
#   * 客户端（本机）需要 python3，且能直连 VPS 的 $PORT。
set -u

usage() {
  sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
}

SSH_TARGET=""
# 用字面量 $HOME 而不是 ~：这个字符串会原样发给远端 shell，由远端展开
# shellcheck disable=SC2016
REMOTE_SRC='$HOME/Russia-Tetris/src'
HOST=""
PORT=9999
PLAYERS=800
RUN_SECONDS=30
KEY_DELAY=0.02
WORKERS="1 2 4"
ROUNDS=2
TIMEOUT=120
OUT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --ssh) SSH_TARGET=${2:-}; shift 2 ;;
    --remote-src) REMOTE_SRC=${2:-}; shift 2 ;;
    --host) HOST=${2:-}; shift 2 ;;
    --port) PORT=${2:-9999}; shift 2 ;;
    --players) PLAYERS=${2:-800}; shift 2 ;;
    --seconds) RUN_SECONDS=${2:-30}; shift 2 ;;
    --key-delay) KEY_DELAY=${2:-0.02}; shift 2 ;;
    --workers) WORKERS=${2:-"1 2 4"}; shift 2 ;;
    --rounds) ROUNDS=${2:-2}; shift 2 ;;
    --timeout) TIMEOUT=${2:-120}; shift 2 ;;
    --out) OUT=${2:-}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "未知参数: $1"; usage; exit 1 ;;
  esac
done

if [ -z "$SSH_TARGET" ]; then
  echo "缺少 --ssh user@host"
  usage
  exit 1
fi

# --host 不填时从 ssh 目标里取主机名；用 ssh 别名时请显式给 --host
if [ -z "$HOST" ]; then
  HOST=${SSH_TARGET#*@}
fi
case "$SSH_TARGET" in
  *@*) ;;
  *) echo "提示：--ssh 建议用 user@IP；若用 ssh 别名，请显式给 --host（/dev/tcp 不走 ~/.ssh/config）" ;;
esac

TESTS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$TESTS/.." && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
if [ -z "$OUT" ]; then
  OUT="$ROOT/play-remote-results/$STAMP"
fi
mkdir -p "$OUT" || { echo "无法创建结果目录: $OUT"; exit 1; }
OUT=$(cd "$OUT" && pwd)

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new "$SSH_TARGET")
run_ssh() { "${SSH[@]}" "$1"; }

log() { printf '\n[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

log "检查远程环境"
if ! run_ssh "test -x $REMOTE_SRC/tetris_game" 2>>"$OUT/ssh.log"; then
  echo "远程没有可执行文件: $REMOTE_SRC/tetris_game（先在 VPS 上 make）"
  exit 1
fi
if ! run_ssh "test -f $REMOTE_SRC/../tests/play_test.py" 2>>"$OUT/ssh.log"; then
  echo "远程缺少 tests/play_test.py：确认仓库完整、且 --remote-src 指向 src/"
  exit 1
fi
if ! run_ssh "command -v python3 >/dev/null" 2>>"$OUT/ssh.log"; then
  echo "远程没有 python3"
  exit 1
fi

log "在 VPS 造 $PLAYERS 个哈希账号（800 个约 30~60 秒）..."
if ! run_ssh "cd $REMOTE_SRC && rm -f userdata.csv userdata.csv.lock log*.txt && python3 ../tests/play_test.py --seed $PLAYERS --userdata userdata.csv" 2>>"$OUT/ssh.log"; then
  echo "造账号失败，见 $OUT/ssh.log"
  exit 1
fi

# 取远程 pid 文件：云主机登录 banner 可能混进 stdout，所以先落盘再取最后一行
read_remote_pid() {
  run_ssh "cat $REMOTE_SRC/$1" 2>/dev/null | tail -n 1 | tr -cd '0-9'
}

run_one() {
  local w=$1 r=$2
  local tag="w${w}-r${r}"
  local srv_pid="" samp_pid="" ready=0 rc=0

  log "workers=$w 第 $r 轮：启动服务端"
  run_ssh "cd $REMOTE_SRC || exit 3; rm -f log*.txt remote-server.pid remote-sampler.pid; nohup env TETRIS_WORKERS=$w TETRIS_PORT=$PORT ./tetris_game > remote-server-$tag.log 2>&1 & echo \$! > remote-server.pid" 2>>"$OUT/ssh.log"
  srv_pid=$(read_remote_pid remote-server.pid)
  if [ -z "$srv_pid" ]; then
    echo "无法获取服务端 pid，见 $OUT/ssh.log"
    return 1
  fi

  for _ in $(seq 1 120); do
    if (exec 3<>"/dev/tcp/$HOST/$PORT") 2>/dev/null; then ready=1; break; fi
    sleep 0.5
  done
  if [ "$ready" -ne 1 ]; then
    echo "服务端未就绪: $HOST:$PORT"
    run_ssh "kill $srv_pid 2>/dev/null; true" 2>/dev/null
    return 1
  fi

  log "workers=$w 第 $r 轮：采样 + 压测（$PLAYERS 玩家 / ${RUN_SECONDS}s）"
  run_ssh "cd $REMOTE_SRC || exit 3; nohup python3 ../tests/sample_proc.py $srv_pid remote-samples-$tag.json 1 >/dev/null 2>&1 & echo \$! > remote-sampler.pid" 2>>"$OUT/ssh.log"
  samp_pid=$(read_remote_pid remote-sampler.pid)
  sleep 2

  python3 "$TESTS/play_test.py" --host "$HOST" --port "$PORT" \
    --players "$PLAYERS" --seconds "$RUN_SECONDS" --key-delay "$KEY_DELAY" \
    --timeout "$TIMEOUT" --json "$OUT/play-$tag.json" 2>&1 | tee "$OUT/play-$tag.log"
  rc=${PIPESTATUS[0]}

  # 停采样（SIGTERM 会写 JSON），再停服务端；最后兜底清理残留 worker
  run_ssh "kill $samp_pid 2>/dev/null; sleep 1; kill $srv_pid 2>/dev/null; sleep 1; pgrep -x tetris_game >/dev/null 2>&1 && pkill -x tetris_game; true" 2>>"$OUT/ssh.log"

  for _ in $(seq 1 20); do
    if ! (exec 3<>"/dev/tcp/$HOST/$PORT") 2>/dev/null; then break; fi
    sleep 0.5
  done

  # 样本是采样进程退出时才写盘的，等文件出现再取
  for _ in $(seq 1 10); do
    if run_ssh "test -s $REMOTE_SRC/remote-samples-$tag.json" 2>/dev/null; then break; fi
    sleep 0.5
  done
  run_ssh "cat $REMOTE_SRC/remote-samples-$tag.json" > "$OUT/samples-$tag.json" 2>>"$OUT/ssh.log" || true
  run_ssh "cat $REMOTE_SRC/remote-server-$tag.log" > "$OUT/server-$tag.log" 2>>"$OUT/ssh.log" || true
  run_ssh "cd $REMOTE_SRC && rm -f remote-samples-$tag.json remote-server-$tag.log remote-server.pid remote-sampler.pid" 2>/dev/null || true

  if [ ! -s "$OUT/samples-$tag.json" ]; then
    echo "警告：没取到第 $r 轮的采样数据（$tag）"
  fi
  if [ "$rc" -ne 0 ]; then
    echo "workers=$w 第 $r 轮压测失败（rc=$rc），日志: $OUT/play-$tag.log"
    return 1
  fi
  return 0
}

reverse_list() {
  local out="" w
  for w in $WORKERS; do out="$w $out"; done
  printf '%s\n' "$out"
}

failed=0
for r in $(seq 1 "$ROUNDS"); do
  if [ $((r % 2)) -eq 1 ]; then
    run_list=$WORKERS
  else
    run_list=$(reverse_list)
  fi
  for w in $run_list; do
    run_one "$w" "$r" || failed=$((failed + 1))
  done
done

if ! python3 "$TESTS/summarize_play.py" "$OUT" "$PLAYERS" "$RUN_SECONDS"; then
  failed=$((failed + 1))
fi

echo "结果目录: $OUT"
if [ "$failed" -ne 0 ]; then
  echo "有 $failed 项失败（每轮日志与 server-*.log 都在结果目录里）"
  exit 1
fi
