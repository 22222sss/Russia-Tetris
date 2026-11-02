#!/usr/bin/env bash
# 把 Russia-Tetris 服务端装成 systemd 服务（生产形态）。
#
# 七步：装二进制/账号/目录、装 systemd 单元（含每日备份 timer）、装 sysctl、
# 启动并自检。反复执行是安全的（幂等），升级时重跑一次即可。
#
#   cd src && make && cd ..
#   sudo deploy/install-systemd.sh
#   sudo deploy/install-ssh-bridge.sh        # SSH 入口（另一个脚本，公网只需要它）
#
# 可选环境变量：
#   TETRIS_BIN_DIR   二进制所在目录（默认 <仓库>/src）
#   SKIP_SYSCTL=1    不装/不改 sysctl（容器、或已有统一配置时）
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
BIN_DIR="${TETRIS_BIN_DIR:-$REPO_ROOT/src}"
UNIT_DIR="/etc/systemd/system"
# 账号名和单元里的 User=/Group= 必须一致，所以这里不做成环境变量
SERVICE_USER="tetrisd"
SERVICE_GROUP="tetrisd"
DATA_DIR="/var/lib/tetris"
LOG_DIR="/var/log/tetris"
BACKUP_DIR="/var/backups/tetris"
DOC_DIR="/usr/local/share/doc/tetris"

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!!\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m!!!\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "需要 root：sudo $0"
command -v systemctl >/dev/null 2>&1 || die "这台机器没有 systemd；容器部署请用 Dockerfile（docs/DEPLOY.md 第 4 节）"
[ -d "$UNIT_DIR" ] || die "找不到 $UNIT_DIR"

for f in tetris_game tetris-admin; do
    [ -x "$BIN_DIR/$f" ] || die "找不到可执行的 $BIN_DIR/$f —— 先 cd src && make"
done
for f in "$HERE/systemd/tetris-game.service" "$HERE/systemd/tetris-backup.service" \
         "$HERE/systemd/tetris-backup.timer" "$HERE/tetris-backup.sh"; do
    [ -f "$f" ] || die "找不到 $f（deploy/ 不完整？）"
done

info "1/7 安装二进制到 /usr/local/bin"
install -m 0755 "$BIN_DIR/tetris_game" /usr/local/bin/tetris_game
install -m 0755 "$BIN_DIR/tetris-admin" /usr/local/bin/tetris-admin
install -m 0755 "$HERE/tetris-backup.sh" /usr/local/bin/tetris-backup

info "2/7 检查服务账号 $SERVICE_USER"
if id "$SERVICE_USER" >/dev/null 2>&1; then
    info "账号已存在，保留原 uid/home"
else
    # 服务账号不需要 shell（systemd 直接 exec），nologin 也避免被当成登录账号
    useradd --system --user-group --home-dir "$DATA_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
    info "已创建系统账号 $SERVICE_USER"
fi
ACTUAL_GROUP="$(id -gn "$SERVICE_USER")"
if [ "$ACTUAL_GROUP" != "$SERVICE_GROUP" ]; then
    die "账号 $SERVICE_USER 的主组是 $ACTUAL_GROUP，而单元里写的是 Group=$SERVICE_GROUP。
       要么把单元的 Group= 改成 $ACTUAL_GROUP，要么先 usermod -g $SERVICE_GROUP $SERVICE_USER。"
fi

info "3/7 建目录（库 0700 / 日志 0750 / 备份 0700，全部属于 $SERVICE_USER）"
install -d -m 0700 -o "$SERVICE_USER" -g "$SERVICE_GROUP" "$DATA_DIR"
install -d -m 0750 -o "$SERVICE_USER" -g "$SERVICE_GROUP" "$LOG_DIR"
install -d -m 0700 -o "$SERVICE_USER" -g "$SERVICE_GROUP" "$BACKUP_DIR"
install -d -m 0755 "$DOC_DIR"
install -m 0644 "$REPO_ROOT/docs/DEPLOY.md" "$DOC_DIR/DEPLOY.md"
install -m 0644 "$REPO_ROOT/docs/SSH_DEPLOY.md" "$DOC_DIR/SSH_DEPLOY.md"

info "4/7 安装 systemd 单元"
install -m 0644 "$HERE/systemd/tetris-game.service" "$UNIT_DIR/tetris-game.service"
install -m 0644 "$HERE/systemd/tetris-backup.service" "$UNIT_DIR/tetris-backup.service"
install -m 0644 "$HERE/systemd/tetris-backup.timer" "$UNIT_DIR/tetris-backup.timer"
systemctl daemon-reload

if [ "${SKIP_SYSCTL:-0}" != "1" ]; then
    info "5/7 安装 sysctl（listen backlog / 系统 fd 上限）"
    install -m 0644 "$HERE/sysctl.d/99-tetris.conf" /etc/sysctl.d/99-tetris.conf
    if sysctl --system >/dev/null 2>&1; then
        info "sysctl 已生效（net.core.somaxconn=$(sysctl -n net.core.somaxconn)）"
    else
        warn "sysctl --system 有失败项，请手工检查 /etc/sysctl.d/99-tetris.conf"
    fi
else
    info "5/7 跳过 sysctl（SKIP_SYSCTL=1）"
fi

info "6/7 启动服务（重启也会应用新二进制）"
# 之前手工跑的 ./tetris_game 还占着 9999 的话，systemd 里会 bind 失败。
# 注意别写成 "ss | awk | grep -q"：本脚本开了 pipefail，grep -q 提前退出会让上游
# 收到 SIGPIPE、整条管道变成 141，于是"其实占着端口"被误报成"没占"。
LISTEN_BEFORE="$(ss -ltn 2>/dev/null | awk '$4 ~ /:9999$/ {print $4}' || true)"
if [ -n "$LISTEN_BEFORE" ]; then
    warn "已经有进程在监听 9999（$LISTEN_BEFORE，可能是手工启动的旧实例），服务可能起不来：先把它停掉"
fi
systemctl enable tetris-game.service >/dev/null
systemctl restart tetris-game.service
# 启动是异步的：等最多 10 秒，别用固定 sleep 制造"机器慢就误报"
for _ in 1 2 3 4 5 6 7 8 9 10; do
    systemctl is-active --quiet tetris-game.service && break
    sleep 1
done
if ! systemctl is-active --quiet tetris-game.service; then
    systemctl status --no-pager -l tetris-game.service || true
    die "tetris-game 没起来：看上面的输出，或 journalctl -u tetris-game -n 50"
fi
systemctl enable --now tetris-backup.timer >/dev/null

info "7/7 自检"
LIMIT=$(systemctl show tetris-game.service -p LimitNOFILE --value 2>/dev/null || true)
LISTEN_AFTER="$(ss -ltn 2>/dev/null | awk '$4 ~ /:9999$/ {print $4}' || true)"
echo "    服务的 LimitNOFILE = ${LIMIT:-未知}（期望 300000:300000 —— 这是连接数的天花板）"
echo "    实际进程限制       = $(awk '/Max open files/ {print $4"/"$5}' "/proc/$(systemctl show tetris-game.service -p MainPID --value)/limits" 2>/dev/null || echo 未知)"
echo "    监听地址           = ${LISTEN_AFTER:-未知}"
echo "    服务单元文件       = /etc/systemd/system/tetris-game.service"
echo "    启动打印的生效配置 = journalctl -u tetris-game -n 40 --no-pager"

if [ -z "$LIMIT" ] || [ "$LIMIT" = "infinity" ]; then
    warn "没能确认 LimitNOFILE，请手工核对：systemctl show tetris-game -p LimitNOFILE"
fi
case "$LISTEN_AFTER" in
    127.0.0.1:9999) ;;
    # 0.0.0.0:9999 / *:9999 / 空（没起来）都算不合格：明文端口不该对公网可见
    *) warn "9999 不是只绑 127.0.0.1（当前 '${LISTEN_AFTER:-空}'）：检查 TETRIS_BIND（服务端不该对公网暴露明文端口）" ;;
esac

cat <<EOF

完成。接下来（公网入口）：

  1) 装 SSH 入口、设登录口令（玩家 ssh 进来直接是游戏）：
       sudo deploy/install-ssh-bridge.sh
       sudo passwd tetris

  2) 从另一台机器验证：
       ssh tetris@<公网IP>                  # 应直接进主菜单
       ss -ltn | grep 9999                  # 期望 127.0.0.1:9999

  3) 备份（timer 已开，每天 04:15；也可以立刻跑一次验证）：
       sudo systemctl start tetris-backup.service
       sudo journalctl -u tetris-backup -n 20

  上线 checklist、日志轮转、升级/回滚、排错表：$DOC_DIR/DEPLOY.md
EOF
