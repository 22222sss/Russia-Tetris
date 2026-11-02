#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tetris-admin（管理命令行）的自动化测试。

为什么值得单独测：这是"线上出事了才用"的工具，平时没人跑 —— 一旦它坏了，正好是
最需要它的时候（忘密码的用户在等）。所以每条子命令都在真库上跑一遍，并用测试侧
的 credential.py 复算哈希（与 PasswordHash.cpp 同一格式），证明"重置出来的密码"
确实能通过真实校验，而不是写进去一串看起来像哈希的东西。

覆盖：
1. reset-password：哈希账号 / 旧明文账号（重置即迁移）/ 短密码被拒（退出码 3）/
   未知账号（退出码 2）/ 与当前密码相同（不写库）；
2. backup：内容条数一致、integrity_check ok、权限 600、已存在时拒绝、--force 覆盖，
   以及备份文件里的凭据能读出来（不是个空壳）；
3. list-users：列出账号、标记"还没迁移的旧明文"；
4. stats：账号数 / 成绩条数 / 各难度最高分；
5. 用法与退出码：--help、无参数、未知命令；
6. （--server-bin 时）服务端启动打印的"生效配置"：TETRIS_BIND、DB 路径，
   以及一个非默认的 TETRIS_AUTH_IP_* 确实印的是生效值而不是默认值。

用法（在 src/ 下跑，和服务端同一个目录口径）：
    python3 ../tests/admin_test.py --bin ./tetris-admin --server-bin ./tetris_game
"""

import argparse
import os
import socket
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time

import credential
import db


class Failure(Exception):
    """断言失败。"""


def annotate(level, message):
    msg = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::%s::%s" % (level, msg), flush=True)


def free_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


class Admin:
    """一次调用 tetris-admin；TETRIS_DB 固定指向本次测试的库。"""

    def __init__(self, binary, db_path):
        self.binary = binary
        self.db_path = db_path
        self.env = dict(os.environ)
        self.env["TETRIS_DB"] = db_path
        # 让库层的调试输出别混进断言用的 stdout（我们只断言 stdout + 退出码）
        self.env.pop("TETRIS_LOG_LEVEL", None)

    def run(self, *args, stdin_text="", timeout=120):
        proc = subprocess.run(
            [self.binary] + list(args),
            input=stdin_text.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=self.env,
            timeout=timeout)
        return (proc.returncode,
                proc.stdout.decode("utf-8", "replace"),
                proc.stderr.decode("utf-8", "replace"))


LEGACY_USER = "legacy-admin"      # 旧版明文账号（第一次登录才会迁移）
HASHED_USER = "hashed-admin"      # 已经是哈希的新账号
PENDING_USER = "pending-admin"    # 故意留着不重置，用来验证"明文（未迁移）"标记
LEGACY_PW = "plain-pw-1234"
HASHED_PW = "hashed-pw-1234"
PENDING_PW = "pending-pw-1234"

STATE = {"stage": "初始化"}


def stage(name):
    STATE["stage"] = name


def seed(db_path):
    """造三个账号 + 若干成绩（直接写库，模拟"已经在跑的老库"）。"""
    db.insert_account(db_path, LEGACY_USER, LEGACY_PW)          # 明文
    db.insert_account(db_path, HASHED_USER, credential.hash_password(HASHED_PW))
    db.insert_account(db_path, PENDING_USER, PENDING_PW)        # 明文，且不重置
    db.insert_score(db_path, HASHED_USER, "EASY", 4321, "2026-11-02 20:00")
    db.insert_score(db_path, HASHED_USER, "EASY", 999, "2026-11-02 20:01")
    db.insert_score(db_path, HASHED_USER, "NORMAL", 777, "2026-11-02 20:02")


def stored(db_path, user):
    return db.read_credential(db_path, user)


def expect_hash_of(db_path, user, password):
    value = stored(db_path, user)
    if not credential.is_hashed(value or ""):
        raise Failure("[%s] %r 的凭据不是哈希：%r" % (STATE["stage"], user, value))
    if not credential.verify(password, value):
        raise Failure("[%s] 落盘哈希匹配不上新密码" % STATE["stage"])
    return value


# ---------------------------------------------------------------------------
# reset-password
# ---------------------------------------------------------------------------

def test_reset_hashed(admin, db_path):
    stage("重置哈希账号的密码")
    new_pw = "reset-hashed-1"
    rc, out, err = admin.run("reset-password", HASHED_USER,
                             "--password-stdin", stdin_text=new_pw + "\n")
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    if "已重置" not in out:
        raise Failure("成功时 stdout 没有确认信息：%r" % out)
    value = expect_hash_of(db_path, HASHED_USER, new_pw)
    if credential.verify(HASHED_PW, value):
        raise Failure("旧密码竟然仍然有效")
    if "不会被踢下线" not in out:
        raise Failure("没有提示'改密不影响已登录会话'（6.27 的已知行为）")


def test_reset_legacy_migrates(admin, db_path):
    stage("重置旧明文账号（重置即迁移）")
    new_pw = "reset-legacy-1"
    rc, out, err = admin.run("reset-password", LEGACY_USER,
                             "--password-stdin", stdin_text=new_pw + "\n")
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    value = expect_hash_of(db_path, LEGACY_USER, new_pw)
    if credential.verify(LEGACY_PW, value):
        raise Failure("旧明文密码仍然有效")


def test_reset_short_rejected(admin, db_path):
    stage("短密码必须被拒（与注册同一套策略）")
    before = stored(db_path, PENDING_USER)
    rc, out, err = admin.run("reset-password", PENDING_USER,
                             "--password-stdin", stdin_text="short\n")
    if rc != 3:
        raise Failure("退出码 %d（期望 3）；stdout=%r stderr=%r" % (rc, out, err))
    if "至少 8" not in err:
        raise Failure("没有说明密码策略：%r" % err)
    if stored(db_path, PENDING_USER) != before:
        raise Failure("密码被拒绝了却仍然改了库")


def test_reset_unknown(admin, db_path):
    stage("未知账号必须报错且不动库")
    before = db.count_accounts(db_path)
    rc, out, err = admin.run("reset-password", "no-such-user-admin",
                             "--password-stdin", stdin_text="whatever-pw-1\n")
    if rc != 2:
        raise Failure("退出码 %d（期望 2）；stderr=%r" % (rc, err))
    if "没有这个账号" not in err:
        raise Failure("错误提示不够明确：%r" % err)
    if db.count_accounts(db_path) != before:
        raise Failure("未知账号的重置竟然写进了库")


def test_reset_same_password(admin, db_path):
    stage("新密码与当前密码相同 -> 不写库")
    before = stored(db_path, HASHED_USER)      # 上一条用例刚改过
    current_pw = "reset-hashed-1"
    rc, out, err = admin.run("reset-password", HASHED_USER,
                             "--password-stdin", stdin_text=current_pw + "\n")
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    if "相同" not in out:
        raise Failure("没有提示'与当前密码相同'：%r" % out)
    if stored(db_path, HASHED_USER) != before:
        raise Failure("说是没改，实际写了库")


# ---------------------------------------------------------------------------
# backup
# ---------------------------------------------------------------------------

def test_backup(admin, db_path, workdir):
    stage("在线备份")
    target = os.path.join(workdir, "backup.db")
    rc, out, err = admin.run("backup", target)
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    if not os.path.exists(target):
        raise Failure("备份文件没生成：%s" % target)
    if "integrity_check ok" not in out:
        raise Failure("输出里没有 integrity_check 结果：%r" % out)

    if db.count_accounts(target) != db.count_accounts(db_path):
        raise Failure("备份里的账号数和源库不一致")
    con = sqlite3.connect(target)
    try:
        if con.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise Failure("备份的 integrity_check 不是 ok")
        source_scores = db.scores_for(db_path, HASHED_USER)
        copied_scores = db.scores_for(target, HASHED_USER)
        if source_scores != copied_scores:
            raise Failure("备份里的成绩和源库不一致：%r vs %r"
                          % (source_scores, copied_scores))
    finally:
        con.close()

    # 备份不是空壳：凭据能读出来，而且仍是合法哈希
    value = db.read_credential(target, HASHED_USER)
    if not credential.is_hashed(value or ""):
        raise Failure("备份里的凭据读不出来：%r" % value)

    # 库里有密码哈希，备份必须只给 owner 读写。Windows 的 chmod 只有只读位，
    # 600 这个语义只在 POSIX 上能断言（CI 是 Linux）。
    if os.name == "posix":
        mode = stat.S_IMODE(os.stat(target).st_mode)
        if mode != 0o600:
            raise Failure("备份文件权限不是 600（里面有密码哈希）：%o" % mode)

    stage("目标已存在时拒绝，--force 才覆盖")
    rc, out, err = admin.run("backup", target)
    if rc != 2 or "已存在" not in err:
        raise Failure("没有拒绝覆盖（退出码 %d，stderr=%r）" % (rc, err))
    rc, out, err = admin.run("backup", target, "--force")
    if rc != 0:
        raise Failure("--force 仍然失败（退出码 %d，stderr=%r）" % (rc, err))


# ---------------------------------------------------------------------------
# list-users / stats
# ---------------------------------------------------------------------------

def test_list_users(admin):
    stage("list-users")
    rc, out, err = admin.run("list-users", "--limit", "0")
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    for user in (HASHED_USER, LEGACY_USER, PENDING_USER):
        if user not in out:
            raise Failure("列表里没有 %r：%r" % (user, out))

    pending_lines = [line for line in out.splitlines() if PENDING_USER in line]
    if not pending_lines:
        raise Failure("找不到 %r 那一行：%r" % (PENDING_USER, out))
    if "明文" not in pending_lines[0]:
        raise Failure("没把还没迁移的账号标成明文：%r" % pending_lines[0])
    hashed_lines = [line for line in out.splitlines() if HASHED_USER in line]
    if not hashed_lines or "哈希" not in hashed_lines[0]:
        raise Failure("已迁移的账号没标成哈希：%r" % hashed_lines)


def test_stats(admin):
    stage("stats")
    rc, out, err = admin.run("stats")
    if rc != 0:
        raise Failure("退出码 %d（期望 0）；stderr=%r" % (rc, err))
    for needle in ("账号数：", "成绩条数：", "EASY"):
        if needle not in out:
            raise Failure("stats 输出缺少 %r：%r" % (needle, out))
    if "4321" not in out:
        raise Failure("EASY 最高分（4321）没有出现在 stats 里：%r" % out)


# ---------------------------------------------------------------------------
# 用法 / 退出码
# ---------------------------------------------------------------------------

def test_usage(admin):
    stage("用法与退出码")
    rc, out, _ = admin.run("--help")
    if rc != 0 or "reset-password" not in out:
        raise Failure("--help 不正常（退出码 %d）" % rc)
    rc, _, _ = admin.run()
    if rc != 2:
        raise Failure("无参数时退出码应为 2，实际 %d" % rc)
    rc, _, err = admin.run("no-such-command")
    if rc != 2 or "未知命令" not in err:
        raise Failure("未知命令没有报错（退出码 %d，stderr=%r）" % (rc, err))


# ---------------------------------------------------------------------------
# 服务端启动打印的"生效配置"（可选：需要真二进制）
# ---------------------------------------------------------------------------

def read_until(proc, marker, timeout):
    """从子进程 stdout 里读到 marker 出现（或超时），返回累计文本。"""
    collected = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = proc.stdout.read(1)
        if not chunk:
            break
        collected += chunk
        if marker.encode("utf-8") in collected:
            break
    return collected.decode("utf-8", "replace")


def test_startup_config(server_bin, workdir):
    stage("服务端启动打印生效配置")
    if not server_bin or not os.path.exists(server_bin):
        raise Failure("找不到服务端二进制：%r（用 --server-bin 指定）" % server_bin)
    # 下面 Popen 的 cwd 是 workdir（让服务端在自己的目录里建库/写日志），
    # 所以二进制路径必须先转成绝对路径，否则 ./tetris_game 会相对 workdir 找不到。
    server_bin = os.path.abspath(server_bin)

    db_path = os.path.join(workdir, "server.db")
    env = dict(os.environ)
    env.update({
        "TETRIS_PORT": str(free_port()),
        "TETRIS_BIND": "127.0.0.1",
        "TETRIS_WORKERS": "1",
        "TETRIS_DB": db_path,
        # 故意设成非默认值：打印的必须是"生效值"，不是默认值
        "TETRIS_AUTH_IP_MAX_FAILURES": "77",
    })
    proc = subprocess.Popen([server_bin], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, cwd=workdir)
    try:
        text = read_until(proc, "连接池", timeout=30)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        proc.stdout.close()

    if "生效配置" not in text:
        raise Failure("启动时没有打印生效配置；输出：%r" % text[-600:])
    if "127.0.0.1" not in text:
        raise Failure("生效配置里看不到 TETRIS_BIND=127.0.0.1：%r" % text[-600:])
    if db_path not in text:
        raise Failure("生效配置里看不到 TETRIS_DB 的实际路径：%r" % text[-600:])
    if "77 次" not in text:
        raise Failure("TETRIS_AUTH_IP_MAX_FAILURES=77 没有体现出来（印成了默认值？）：%r"
                      % text[-600:])


# ---------------------------------------------------------------------------

CHECKS = [
    ("重置哈希账号密码", test_reset_hashed),
    ("重置旧明文账号（迁移）", test_reset_legacy_migrates),
    ("短密码被拒", test_reset_short_rejected),
    ("未知账号报错", test_reset_unknown),
    ("与当前密码相同不写库", test_reset_same_password),
    ("在线备份 + 校验 + 权限", test_backup),
    ("list-users", test_list_users),
    ("stats", test_stats),
    ("用法与退出码", test_usage),
]


def main():
    parser = argparse.ArgumentParser(description="tetris-admin 的自动化测试")
    parser.add_argument("--bin", default="./tetris-admin", help="tetris-admin 二进制")
    parser.add_argument("--server-bin", default="./tetris_game",
                        help="服务端二进制（只用于验证启动时的生效配置打印）")
    parser.add_argument("--workdir", default="",
                        help="测试用的工作目录（默认临时目录；指定时会复用同一个库）")
    args = parser.parse_args()

    if not os.path.exists(args.bin):
        annotate("error", "找不到 %s（先在 src/ 下 make）" % args.bin)
        return 1

    workdir = args.workdir or tempfile.mkdtemp(prefix="admin-test-")
    os.makedirs(workdir, exist_ok=True)
    # 复用同一工作目录时要先清掉上一轮的产物，否则 backup 会撞"文件已存在"
    for base in ("tetris.db", "server.db", "backup.db"):
        for suffix in ("", "-wal", "-shm"):
            try:
                os.unlink(os.path.join(workdir, base + suffix))
            except OSError:
                pass
    db_path = os.path.join(workdir, "tetris.db")
    admin = Admin(args.bin, db_path)

    print("库: %s\n二进制: %s" % (db_path, args.bin), flush=True)
    seed(db_path)

    failed = 0
    for name, fn in CHECKS:
        started = time.time()
        try:
            if fn is test_backup:
                fn(admin, db_path, workdir)
            elif fn in (test_list_users, test_stats, test_usage):
                fn(admin)
            else:
                fn(admin, db_path)
        except Failure as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "管理命令测试失败 - %s: %s" % (name, exc))
        except (OSError, subprocess.SubprocessError) as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "管理命令测试失败 - %s: %s" % (name, exc))
        else:
            print("PASS %s (%.1fs)" % (name, time.time() - started), flush=True)

    # 启动配置打印单独跑：它要起服务端
    name = "服务端启动打印生效配置"
    started = time.time()
    try:
        test_startup_config(args.server_bin, workdir)
    except Failure as exc:
        failed += 1
        print("FAIL %s: %s" % (name, exc), flush=True)
        annotate("error", "管理命令测试失败 - %s: %s" % (name, exc))
    except (OSError, subprocess.SubprocessError) as exc:
        failed += 1
        print("FAIL %s: %s" % (name, exc), flush=True)
        annotate("error", "管理命令测试失败 - %s: %s" % (name, exc))
    else:
        print("PASS %s (%.1fs)" % (name, time.time() - started), flush=True)

    total = len(CHECKS) + 1
    if failed:
        print("管理命令测试失败: %d/%d 项" % (failed, total), flush=True)
        return 1

    print("管理命令测试全部通过: %d/%d 项" % (total, total), flush=True)
    annotate("notice", "管理命令测试全部通过（%d 项）" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
