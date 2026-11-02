#!/usr/bin/env bash
# 安装 Russia-Tetris 的 SSH 入口：桥程序 + sshd Match 片段 + 登录账号。
#
# 只动 SSH 侧，不碰游戏服务端、不碰 SQLite 库、不改端口。
# 反复执行是安全的（幂等）；sshd 校验失败会自动还原，现网配置不会被打坏。
#
#   sudo deploy/install-ssh-bridge.sh
#   sudo TETRIS_LOGIN_PASSWORD='公开的登录口令' deploy/install-ssh-bridge.sh
#   sudo TETRIS_LOGIN_USER=tetris TETRIS_LOGIN_HOME=/var/lib/tetris-login deploy/install-ssh-bridge.sh
#
# 可选环境变量：BRIDGE_DST / SNIPPET_DST / SSHD_CONFIG / TETRIS_LOGIN_USER /
#               TETRIS_LOGIN_HOME / TETRIS_LOGIN_PASSWORD
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRIDGE_SRC="$HERE/tetris-bridge.py"
SNIPPET_SRC="$HERE/sshd-tetris.conf"

BRIDGE_DST="${BRIDGE_DST:-/usr/local/bin/tetris-bridge}"
SNIPPET_DST="${SNIPPET_DST:-/etc/ssh/sshd_config.d/tetris.conf}"
SSHD_CONFIG="${SSHD_CONFIG:-/etc/ssh/sshd_config}"
LOGIN_USER="${TETRIS_LOGIN_USER:-tetris}"
LOGIN_HOME="${TETRIS_LOGIN_HOME:-/var/lib/tetris-login}"
LOGIN_PASSWORD="${TETRIS_LOGIN_PASSWORD:-}"

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!!\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m!!!\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "需要 root：sudo $0"
[ -f "$BRIDGE_SRC" ] || die "找不到 $BRIDGE_SRC"
[ -f "$SNIPPET_SRC" ] || die "找不到 $SNIPPET_SRC"
command -v python3 >/dev/null 2>&1 || die "桥用 Python 3 标准库实现，请先 apt install python3"

# root 的 PATH 不一定带 /usr/sbin，sshd 常常就在那里
if command -v sshd >/dev/null 2>&1; then
    SSHD=sshd
elif [ -x /usr/sbin/sshd ]; then
    SSHD=/usr/sbin/sshd
else
    die "找不到 sshd：这台机器没装 openssh-server？"
fi

info "1/5 安装桥程序到 $BRIDGE_DST"
install -m 0755 "$BRIDGE_SRC" "$BRIDGE_DST"

info "2/5 检查登录账号 $LOGIN_USER"
if id "$LOGIN_USER" >/dev/null 2>&1; then
    shell=$(getent passwd "$LOGIN_USER" | cut -d: -f7)
    case "$shell" in
        */nologin|*/false)
            # sshd 是用"用户自己的 shell -c ForceCommand"去执行命令的：shell 是 nologin
            # 时 sshd 会先被 nologin 拒绝，桥根本起不来。玩家看到的是
            # "This account is currently not available."（见 docs/SSH_DEPLOY.md 排错表）
            chsh -s /bin/sh "$LOGIN_USER"
            warn "已把 $LOGIN_USER 的 shell 从 $shell 改成 /bin/sh（否则 ForceCommand 不会执行）"
            ;;
    esac
    info "账号已存在：保留它的 uid / home，只保证 shell 可用"
else
    useradd --system --create-home --home-dir "$LOGIN_HOME" --shell /bin/sh "$LOGIN_USER"
    info "已创建系统账号 $LOGIN_USER（home=$LOGIN_HOME）"
fi

if [ -n "$LOGIN_PASSWORD" ]; then
    printf '%s:%s\n' "$LOGIN_USER" "$LOGIN_PASSWORD" | chpasswd
    info "已按 TETRIS_LOGIN_PASSWORD 设置登录口令"
else
    warn "没有设置登录口令，玩家现在连不上：先执行 passwd $LOGIN_USER"
fi

info "3/5 检查 sshd 是否会加载 $SNIPPET_DST"
if ! grep -qE '^[[:space:]]*Include[[:space:]]+/etc/ssh/sshd_config\.d/\*\.conf' "$SSHD_CONFIG"; then
    die "主配置 $SSHD_CONFIG 里没有 'Include /etc/ssh/sshd_config.d/*.conf'。
       请先在它的顶部加这一行（或把 $SNIPPET_SRC 手工追加到主配置），再重跑本脚本。"
fi

info "4/5 安装并校验 sshd 片段"
mkdir -p "$(dirname "$SNIPPET_DST")"
backup=""
if [ -f "$SNIPPET_DST" ]; then
    backup="$SNIPPET_DST.bak.$(date +%Y%m%d%H%M%S)"
    cp -a "$SNIPPET_DST" "$backup"
fi
install -m 0644 "$SNIPPET_SRC" "$SNIPPET_DST"
if ! "$SSHD" -t; then
    if [ -n "$backup" ]; then
        cp -a "$backup" "$SNIPPET_DST"
        die "sshd -t 校验失败，已还原为 $backup；sshd 没有 reload，现网配置未变"
    fi
    rm -f "$SNIPPET_DST"
    die "sshd -t 校验失败，已删除本脚本写入的片段；sshd 没有 reload，现网配置未变"
fi
info "sshd -t 通过"

info "5/5 重新加载 sshd"
if systemctl reload ssh 2>/dev/null || systemctl reload sshd 2>/dev/null; then
    info "sshd 已 reload（已有会话不受影响）"
else
    warn "自动 reload 失败，请手动执行：systemctl reload ssh"
fi

cat <<EOF

完成。接下来三件事：

  1) 服务端只绑回环地址 —— 公网不要再直接暴露 9999（明文）：
       TETRIS_BIND=127.0.0.1 ./tetris_game
     确认：ss -ltnp | grep 9999     # 期望 127.0.0.1:9999

  2) 从另一台机器进游戏：
       ssh $LOGIN_USER@<公网IP>

  3) 部署细节、来源 IP 映射、以及"连上没反应"之类的排错表：
       docs/SSH_DEPLOY.md
EOF
