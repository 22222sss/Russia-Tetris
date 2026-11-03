#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""恢复演练：把一份备份**真的恢复出来跑一遍**，全程不碰生产库。

为什么需要它：`docs/DEPLOY.md` 第 7.4 节写的是"真出事时"的恢复步骤，但
"备份文件能打开"和"恢复之后能玩"是两件事 —— 只读一遍 SQL 是不会发现
"属主不对 / WAL 没清 / 端口起不来 / 库是另一台机器的"这些问题的。
这个脚本在一台已经装好的机器上，用**临时目录 + 临时端口**把同一套流程走一遍：

  1. 把备份复制到临时目录（恢复的第一步就是"把文件放回去"）；
  2. `PRAGMA integrity_check` + 账号/成绩条数，并与生产库对比（只读，不写生产库）；
  3. 用这份副本起一个**临时服务端**（另一个端口、另一个工作目录、另一个日志目录）；
  4. 连上去确认菜单能出来；能找到 `tests/integration_test.py` 时再跑一遍完整协议（12 项）；
  5. 关掉临时实例、删掉临时目录（`--keep` 保留，方便用 `script` 起桥亲手玩这个副本）。

为什么要 root：备份正常是 0600、备份目录 0700，且临时实例要以服务账号（默认 `tetrisd`）
身份运行 —— 只拷文件不换属主，服务端根本写不进去，演练就成了"演了个假的"。
非 root 也能跑（`--user ''`），只是那样验不到"以服务身份运行"这一段。

用法
    sudo deploy/restore-drill.py /var/backups/tetris/tetris-20261102T041500Z.db
    sudo deploy/restore-drill.py <备份> --keep         # 保留实例，接着 ssh 进去玩
    sudo deploy/restore-drill.py <备份> --protocol-test tests/integration_test.py

退出码：0 演练通过；1 演练失败（信息里会指明是哪一步）；2 用法/环境错误
"""

import argparse
import os
import shlex
import shutil
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

try:  # 正式形态在 Linux 上跑；Windows 只用于"用假服务端演练脚本本身"
    import pwd
except ImportError:  # pragma: no cover
    pwd = None

DEFAULT_DB = "/var/lib/tetris/tetris.db"
DEFAULT_SERVER = "/usr/local/bin/tetris_game"
DEFAULT_USER = "tetrisd"
# install-systemd.sh 把协议测试装到这里（装完就没有仓库了，只能从这找）
INSTALLED_TESTS = "/usr/local/share/doc/tetris/tests/integration_test.py"
TOTAL_STEPS = 5


class DrillError(Exception):
    """演练失败（带"哪一步"的上下文）。"""


def info(message):
    print("==> %s" % message, flush=True)


def ok(message):
    print("  ok  %s" % message, flush=True)


def warn(message):
    print("  !!  %s" % message, flush=True)


def read_stats(path):
    """只读打开：integrity_check + 账号/成绩条数。"""
    uri = "file:%s?mode=ro" % path.replace("?", "%3f").replace("#", "%23")
    conn = sqlite3.connect(uri, uri=True, timeout=5)
    try:
        integrity = conn.execute("PRAGMA integrity_check").fetchone()[0]
        accounts = conn.execute("SELECT COUNT(*) FROM accounts").fetchone()[0]
        scores = conn.execute("SELECT COUNT(*) FROM scores").fetchone()[0]
    except sqlite3.Error as exc:
        raise DrillError("读不了这个库（%s）：%s" % (path, exc))
    finally:
        conn.close()
    return integrity, accounts, scores


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def is_root():
    return hasattr(os, "geteuid") and os.geteuid() == 0


def current_user_name():
    if pwd is not None:
        return pwd.getpwuid(os.geteuid()).pw_name
    return os.environ.get("USERNAME") or os.environ.get("USER") or "?"


def resolve_account(name):
    if pwd is None:
        raise DrillError("这台机器没有 pwd（不是 POSIX 系统），只能用 --user '' 演练")
    try:
        entry = pwd.getpwnam(name)
    except KeyError:
        raise DrillError("找不到账号 %s —— 先跑 deploy/install-systemd.sh" % name)
    return entry.pw_uid, entry.pw_gid


def chown_tree(path, uid, gid):
    """把演练目录还给服务账号：协议测试是以 root 跑的，SQLite 可能建出 root 属主的新文件。"""
    if not hasattr(os, "chown"):
        return
    for base, dirs, files in os.walk(path):
        try:
            os.chown(base, uid, gid)
        except OSError:
            pass
        for name in dirs + files:
            try:
                os.chown(os.path.join(base, name), uid, gid)
            except OSError:
                pass


def wait_for_port(port, proc, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def read_menu(port, timeout=5.0):
    """连上去读首屏：恢复出来的实例至少得像服务端一样说话。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1.0) as conn:
                conn.settimeout(2.0)
                chunks = []
                try:
                    while True:
                        data = conn.recv(4096)
                        if not data:
                            break
                        chunks.append(data)
                        if sum(len(c) for c in chunks) > 2048:
                            break
                except OSError:
                    pass
                return b"".join(chunks).decode("utf-8", "replace")
        except OSError:
            time.sleep(0.2)
    return ""


def tail(path, limit=1500):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return "<读不到 %s>" % path
    return data[-limit:].decode("utf-8", "replace")


def run_drill(args):
    backup = os.path.abspath(args.backup)
    production_db = os.path.abspath(args.db)

    if not os.path.isfile(backup):
        raise DrillError("找不到备份文件：%s" % backup)
    if os.path.realpath(backup) == os.path.realpath(production_db):
        raise DrillError("你给的是**生产库本身**，不是备份。生产库要先：\n"
                         "    sudo -u tetrisd env TETRIS_DB=%s tetris-admin backup <目标>"
                         % production_db)
    if not os.access(backup, os.R_OK):
        raise DrillError("读不到 %s（备份是 0600，本脚本通常要 sudo 跑）" % backup)

    # 以什么身份跑临时实例：root 时用服务账号，非 root 就用当前用户
    drill_user = None
    if args.user and args.user.lower() != "none":
        if is_root():
            drill_user = args.user
        else:
            warn("不是 root，临时实例将以当前用户运行（验不到\"以服务身份运行\"这一段）")
    uid = gid = None
    if drill_user:
        uid, gid = resolve_account(drill_user)

    # 一律绝对路径：临时实例的工作目录就是 workdir，任何相对路径都会跟着 cwd 跑偏
    # （HANDOVER 6.43 那个坑：`Popen(cwd=...)` 之后 `./x` 已经不在你以为的地方）
    workdir = os.path.abspath(args.workdir) if args.workdir else \
        tempfile.mkdtemp(prefix="tetris-restore-drill-")
    os.makedirs(workdir, exist_ok=True)
    drill_db = os.path.join(workdir, "drill.db")
    server_log = os.path.join(workdir, "drill-server.log")
    keep = bool(args.keep)
    port = args.port or free_port()

    proc = None
    failures = []
    protocol_ran = False
    try:
        info("1/%d 复制备份到临时目录 %s" % (TOTAL_STEPS, workdir))
        shutil.copyfile(backup, drill_db)
        os.chmod(drill_db, 0o600)
        if drill_user:
            # 只有"换个账号跑临时实例"时才需要收权限/换属主；
            # 非 root 手工演练（--user ''）保持当前用户能读写就行
            os.chmod(workdir, 0o700)
            if hasattr(os, "chown"):
                os.chown(workdir, uid, gid)
                os.chown(drill_db, uid, gid)
        ok("%s（%.1f KiB，属主 %s）" % (
            os.path.basename(drill_db), os.path.getsize(drill_db) / 1024.0,
            drill_user or current_user_name()))

        info("2/%d 检查副本：integrity_check + 条数（并与生产库对比）" % TOTAL_STEPS)
        integrity, accounts, scores = read_stats(drill_db)
        if integrity != "ok":
            raise DrillError("副本 integrity_check = %r（备份已经坏了，别拿去恢复）" % integrity)
        ok("integrity_check = ok；账号 %d 个 / 成绩 %d 条" % (accounts, scores))

        if os.path.isfile(production_db):
            try:
                _, live_accounts, live_scores = read_stats(production_db)
            except DrillError as exc:
                warn("生产库读不了，跳过对比：%s" % exc)
            else:
                if accounts > live_accounts or scores > live_scores:
                    raise DrillError(
                        "副本比生产库还多（备份 %d 账号/%d 成绩 vs 生产 %d/%d）——"
                        "这份备份不是这台机器的，或生产库被换过"
                        % (accounts, scores, live_accounts, live_scores))
                if (accounts, scores) == (live_accounts, live_scores):
                    ok("与生产库一致（%d 账号 / %d 成绩）" % (accounts, scores))
                else:
                    ok("与生产库的差值符合预期：备份 %d 账号/%d 成绩，生产 %d/%d"
                       "（备份之后的新数据，恢复会丢掉这部分）"
                       % (accounts, scores, live_accounts, live_scores))
        else:
            warn("生产库 %s 不存在，跳过对比（只验了备份本身）" % production_db)

        info("3/%d 用副本起临时服务端（端口 %d，工作目录 %s）" % (TOTAL_STEPS, port, workdir))
        if args.server_cmd:
            # 调试用：允许写成一条命令（{port}/{db} 会被替换），比如
            #   --server-cmd 'python3 tests/mock_server.py {port} {db}'
            argv = [part.replace("{port}", str(port)).replace("{db}", drill_db)
                    for part in shlex.split(args.server_cmd, posix=(os.name != "nt"))]
            if not argv:
                raise DrillError("--server-cmd 是空的")
        else:
            argv = [args.server_bin]
            if not os.access(args.server_bin, os.X_OK):
                raise DrillError("服务端二进制不可执行：%s" % args.server_bin)
        # 干净的环境：不继承调用者 shell 里的 TETRIS_*（否则会验成另一套配置）。
        # 只留平台必需的那几个（Windows 上没有 SystemRoot，Winsock 都初始化不了；
        # Linux 上这几个键本来就不存在，等于没写）。
        env = {key: os.environ[key] for key in
               ("SystemRoot", "SYSTEMROOT", "windir", "COMSPEC", "TEMP", "TMP")
               if key in os.environ}
        env.update({
            "PATH": os.environ.get("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"),
            "LANG": "C.UTF-8",
            "HOME": workdir,
            "TETRIS_DB": drill_db,
            "TETRIS_PORT": str(port),
            "TETRIS_BIND": "127.0.0.1",
            "TETRIS_WORKERS": "1",
            "TETRIS_LOG_DIR": workdir,
            "TETRIS_LOG_LEVEL": "warn",
        })
        with open(server_log, "wb") as log_handle:
            proc = subprocess.Popen(
                argv, cwd=workdir, env=env,
                stdout=log_handle, stderr=subprocess.STDOUT,
                start_new_session=True,
                **({"user": uid, "group": gid} if drill_user else {}))
        if not wait_for_port(port, proc, args.timeout):
            raise DrillError("临时实例 %d 秒内没监听 127.0.0.1:%d；服务端输出：\n%s"
                             % (args.timeout, port, tail(server_log)))
        ok("临时实例已监听 127.0.0.1:%d（pid %d）" % (port, proc.pid))

        info("4/%d 验证它真的能服务：读首屏菜单" % TOTAL_STEPS)
        menu = read_menu(port)
        if not menu:
            raise DrillError("连上了但读不到任何字节；服务端输出：\n%s" % tail(server_log))
        for token in ("注册", "登录"):
            if token not in menu:
                raise DrillError("首屏里没有 %r，不像正常的初始菜单（前 300 字节：%r）"
                                 % (token, menu[:300]))
        ok("首屏菜单正常（%d 字节，含\"注册\"/\"登录\"）" % len(menu))

        if args.protocol_test:
            proto = os.path.abspath(args.protocol_test)
            if not os.path.isfile(proto):
                warn("找不到协议测试 %s，跳过（只验到首屏菜单）" % proto)
            else:
                info("5/%d 跑完整协议（%s）" % (TOTAL_STEPS, os.path.basename(proto)))
                cmd = [sys.executable, proto,
                       "--host", "127.0.0.1", "--port", str(port), "--db", drill_db]
                # 协议测试**以调用者身份**跑（通常就是 root），不切到服务账号：
                # 它要往演练库里写"旧明文账号"，而仓库可能在 /home/xxx 下，
                # 服务账号本来就读不到（线上更该如此）。临时实例才是 tetrisd 身份。
                result = subprocess.run(
                    cmd, cwd=workdir, env=env, text=True, timeout=args.protocol_timeout,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if result.returncode != 0:
                    raise DrillError("协议测试失败（rc=%d）；输出尾部：\n%s"
                                     % (result.returncode, result.stdout[-1500:]))
                if drill_user:
                    chown_tree(workdir, uid, gid)
                protocol_ran = True
                ok("协议测试通过（注册/登录/进游戏都不依赖生产库）")
        else:
            print("  -- 5/%d 跳过协议测试（没有 tests/integration_test.py；"
                  "人力用 ssh 连进去玩更直接，见下面提示）" % TOTAL_STEPS, flush=True)
    except (DrillError, subprocess.SubprocessError, OSError) as exc:
        failures.append(str(exc))
    finally:
        # --keep 且演练成功时才把临时实例留着（失败还留着就是一堆垃圾进程）
        stop_instance = proc is not None and proc.poll() is None and not (keep and not failures)
        if stop_instance:
            # 先 SIGTERM 让服务端按自己的退出路径走，再不行才 SIGKILL
            # （Windows 没有 SIGKILL，本地演练用 terminate/kill 顶上）
            stop_signals = [getattr(signal, "SIGTERM", None)]
            if hasattr(signal, "SIGKILL"):
                stop_signals.append(signal.SIGKILL)
            for sig in stop_signals:
                if sig is None:
                    continue
                try:
                    if hasattr(os, "killpg"):
                        os.killpg(os.getpgid(proc.pid), sig)
                    else:  # pragma: no cover - Windows 本地演练
                        proc.send_signal(sig)
                    proc.wait(timeout=10)
                    break
                except (OSError, subprocess.TimeoutExpired):
                    continue
        if not keep:
            shutil.rmtree(workdir, ignore_errors=True)

    if failures:
        print("\n恢复演练失败：%s" % failures[-1], flush=True)
        print("临时目录保留在 %s（排查完自己删）" % workdir, flush=True)
        return 1

    print("\n恢复演练通过：这份备份可以直接拿去恢复（integrity ok、临时实例能服务%s）。"
          % ("、完整协议通过" if protocol_ran else ""), flush=True)
    if keep:
        print("临时实例仍在运行：127.0.0.1:%d，目录 %s\n"
              "  用你现在的终端直接玩这个副本（桥跑在**服务器侧**，所以环境变量加在服务器这边；\n"
              "  从客户端 ssh 传进来的环境变量 sshd 默认不收）：\n"
              "    sudo -u tetris TETRIS_BRIDGE_TARGET=127.0.0.1:%d "
              "script -q -c /usr/local/bin/tetris-bridge /dev/null\n"
              "  玩完停掉它：sudo kill %d"
              % (port, workdir, port, proc.pid if proc else 0), flush=True)
    else:
        print("临时目录已清理（想手动玩就加 --keep）", flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="把一份 tetris.db 备份真的恢复出来跑一遍（不碰生产库）")
    parser.add_argument("backup", help="备份文件（tetris-admin backup 的产物）")
    parser.add_argument("--db", default=os.environ.get("TETRIS_DB", DEFAULT_DB),
                        help="生产库路径（只读用于对比，默认 %s）" % DEFAULT_DB)
    parser.add_argument("--server-bin", default=DEFAULT_SERVER,
                        help="服务端二进制（默认 %s）" % DEFAULT_SERVER)
    parser.add_argument("--server-cmd", default="",
                        help="调试用：直接给一条启动命令（{port}/{db} 会被替换），"
                             "例如 --server-cmd 'python3 tests/mock_server.py {port} {db}'")
    parser.add_argument("--user", default=DEFAULT_USER,
                        help="以哪个账号跑临时实例（默认 %s；--user none = 当前用户）" % DEFAULT_USER)
    parser.add_argument("--protocol-test", default="",
                        help="完整协议测试脚本（默认自动找仓库里的 tests/integration_test.py）")
    parser.add_argument("--workdir", default="", help="临时目录（默认 /tmp 下随机）")
    parser.add_argument("--port", type=int, default=0, help="临时实例端口（默认随机空闲端口）")
    parser.add_argument("--keep", action="store_true",
                        help="演练完保留临时实例和目录（方便 ssh 连进去手动玩）")
    parser.add_argument("--timeout", type=int, default=30, help="等临时实例起监听（秒）")
    parser.add_argument("--protocol-timeout", type=int, default=240,
                        help="协议测试超时（秒）")
    args = parser.parse_args()

    if args.port and args.port == 9999:
        print("端口 9999 是生产实例的，演练请用别的端口（或留空自动挑）", file=sys.stderr)
        return 2

    if not args.protocol_test:
        # 两个位置都要找：仓库里的（deploy/ 旁边就是 tests/）和安装后的
        # （install-systemd.sh 把 integration_test.py + 它 import 的两个模块
        # 装到文档目录）。只找前者的话，真机上这条命令会**静默跳过第 5 步**
        # —— 提示是"跳过协议测试"，但结尾照样打印"恢复演练通过"，很容易被当成
        # 全绿（2026-11-03 在 WSL 真机上第一次发现）。
        here = os.path.dirname(os.path.abspath(__file__))
        for guess in (os.path.join(os.path.dirname(here), "tests", "integration_test.py"),
                      INSTALLED_TESTS):
            if os.path.isfile(guess):
                args.protocol_test = guess
                break

    try:
        return run_drill(args)
    except DrillError as exc:
        print("恢复演练失败：%s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
