#!/bin/sh
# 定时备份 tetris.db：由 tetris-backup.timer / tetris-backup.service 调用，
# 也可以手工跑（sudo -u tetrisd TETRIS_DB=... tetris-backup）。
#
# 为什么不是 logrotate 式"自己拷文件"：SQLite 在 WAL 模式下只拷 .db 会丢掉还没
# checkpoint 的写入（见 docs/HANDOVER.md 6.31），所以这里一律走
# `tetris-admin backup`（Online Backup API，运行中也能拿到一致快照）。
#
# 退出码：0 成功；1 备份失败（systemd 会把这次运行标成 failed）；2 配置/环境错误
#
# 环境变量（systemd 单元里已设好，一般不用改）：
#   TETRIS_DB           源库路径
#   TETRIS_ADMIN        tetris-admin 的路径
#   TETRIS_BACKUP_DIR   备份目录（不能和库放同一个分区，见 docs/DEPLOY.md 第 7 节）
#   TETRIS_BACKUP_KEEP  保留份数（默认 14，最少 1）
set -eu

TETRIS_ADMIN="${TETRIS_ADMIN:-/usr/local/bin/tetris-admin}"
TETRIS_DB="${TETRIS_DB:-/var/lib/tetris/tetris.db}"
TETRIS_BACKUP_DIR="${TETRIS_BACKUP_DIR:-/var/backups/tetris}"
TETRIS_BACKUP_KEEP="${TETRIS_BACKUP_KEEP:-14}"

case "$TETRIS_BACKUP_KEEP" in
    ''|*[!0-9]*)
        echo "TETRIS_BACKUP_KEEP 不是非负整数：'$TETRIS_BACKUP_KEEP'" >&2
        exit 2
        ;;
esac
[ "$TETRIS_BACKUP_KEEP" -ge 1 ] || TETRIS_BACKUP_KEEP=1

[ -x "$TETRIS_ADMIN" ] || { echo "找不到可执行的 tetris-admin：$TETRIS_ADMIN" >&2; exit 2; }
[ -f "$TETRIS_DB" ]    || { echo "找不到数据库文件：$TETRIS_DB" >&2; exit 2; }

# 备份里是密码哈希：新文件默认 600，目录权限由部署脚本保证
umask 077
mkdir -p -- "$TETRIS_BACKUP_DIR"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
target="$TETRIS_BACKUP_DIR/tetris-$stamp.db"
n=1
while [ -e "$target" ]; do
    target="$TETRIS_BACKUP_DIR/tetris-$stamp-$n.db"
    n=$((n + 1))
done

echo "备份 $TETRIS_DB -> $target"
if ! TETRIS_DB="$TETRIS_DB" "$TETRIS_ADMIN" backup "$target"; then
    # 半成品不能留在备份目录里：否则"目录里有 N 份"会骗人
    rm -f -- "$target"
    echo "备份失败，已删除半成品 $target" >&2
    exit 1
fi

if [ ! -s "$target" ]; then
    rm -f -- "$target"
    echo "备份文件为空，已删除 $target" >&2
    exit 1
fi

# 只保留最近 $TETRIS_BACKUP_KEEP 份（按修改时间，新的在前）。
# 用 while read 而不是 for，避免路径里有空格时被拆开。
kept=0
ls -1t "$TETRIS_BACKUP_DIR"/tetris-*.db 2>/dev/null \
    | tail -n "+$((TETRIS_BACKUP_KEEP + 1))" \
    | while IFS= read -r old; do
        rm -f -- "$old"
        echo "删除旧备份 $old"
      done

kept=$(ls -1 "$TETRIS_BACKUP_DIR"/tetris-*.db 2>/dev/null | wc -l | tr -d ' ')
echo "完成：$target（$(du -h -- "$target" | cut -f1)）；备份目录现有 $kept 份，保留上限 $TETRIS_BACKUP_KEEP 份"
