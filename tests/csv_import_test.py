#!/usr/bin/env python3
"""验证"旧 userdata.csv -> SQLite 一次性导入"。

做法分两步（服务端必须在中间启动，导入发生在它打开空库的时候）：

  1) 起服务端**之前**，造一份旧格式 CSV：
       python3 tests/csv_import_test.py --seed-csv userdata.csv \
           --user legacy1 --password plain-pw-123 --score 4321
  2) 起服务端（工作目录 = 这个 CSV 所在目录），然后：
       python3 tests/csv_import_test.py --host 127.0.0.1 --port 10008 \
           --db tetris.db --user legacy1 --password plain-pw-123

断言：
  * 账号与成绩都被导入（成绩条数对得上）；
  * 密码仍是原样（明文）——迁移发生在**登录时**，不是导入时；
  * 用旧明文密码能登录（兼容路径仍然有效）；
  * 登录后库里的密码变成该密码的加盐哈希（迁移写进了 SQLite）。
"""

import argparse
import os
import sys

import credential
import db
import integration_test as it

HEADER = ("Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
          "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
          "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores")


def seed_csv(path, user, password, score):
    exists = os.path.exists(path) and os.path.getsize(path) > 0
    with open(path, "a", encoding="utf-8") as fh:
        if not exists:
            fh.write(HEADER + "\n")
        # 第 12 个字段起是"最近成绩"对（分数,难度），这里故意留空：
        # 只验证最高分那条会不会被导入。
        fh.write("%s,%s,%d,EASY,2026-01-02 03:04:05,0,NORMAL,,0,DIFFICULT,,\n"
                 % (user, password, score))
    print("已写入旧 CSV: %s（账号 %s，EASY 最高分 %d）" % (path, user, score), flush=True)


def main():
    parser = argparse.ArgumentParser(description="旧 userdata.csv 导入验证")
    parser.add_argument("--seed-csv", default="", help="只造 CSV，不连服务端")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=10008)
    parser.add_argument("--db", default="tetris.db")
    parser.add_argument("--user", default="legacy-csv")
    parser.add_argument("--password", default="plain-pw-123")
    parser.add_argument("--score", type=int, default=4321)
    args = parser.parse_args()

    if args.seed_csv:
        seed_csv(args.seed_csv, args.user, args.password, args.score)
        return 0

    def fail(message):
        it.annotate("error", "旧 CSV 导入测试失败: %s" % message)
        print("FAIL 旧 CSV 导入: %s" % message, flush=True)

    # 1) 导入结果：账号在库里，密码还是明文（迁移留给登录）
    if not db.wait_for_account(args.db, args.user, 10.0):
        fail("导入后库里没有账号 %r（先确认服务端是在空库 + 有 userdata.csv 时启动的）"
             % args.user)
        return 1
    stored = db.read_credential(args.db, args.user)
    if stored != args.password:
        fail("导入时不该改动密码（期望明文 %r，实际 %r）" % (args.password, stored))
        return 1

    scores = db.scores_for(args.db, args.user)
    if not any(score == args.score for score, _difficulty in scores):
        fail("EASY 最高分没有导入（库里 %r）" % (scores,))
        return 1

    # 2) 旧明文密码仍能登录
    it.STAGE = "旧 CSV 导入：用明文密码登录"
    conn = it.Conn(args.host, args.port, 10.0)
    try:
        it.expect(conn, "1. 注册帐号")
        conn.sendline("2")
        it.expect(conn, "请输入用户名")
        conn.sendline(args.user)
        it.expect(conn, "请输入密码")
        conn.sendline(args.password)
        it.expect(conn, "3. 开始游戏", timeout=10.0)
    except it.Failure as exc:
        fail(str(exc))
        return 1
    finally:
        conn.close()

    # 3) 登录后迁移：库里变成加盐哈希
    migrated = db.wait_for_credential(
        args.db, args.user,
        lambda value: credential.is_hashed(value) and credential.verify(args.password, value),
        10.0)
    if migrated is None:
        fail("登录后密码没有迁移成加盐哈希（库里 %r）" % db.read_credential(args.db, args.user))
        return 1
    if migrated == args.password:
        fail("密码仍是明文")
        return 1

    print("PASS 旧 CSV 导入（账号 + 成绩已入库，登录后迁移为加盐哈希）", flush=True)
    it.annotate("notice", "旧 userdata.csv 导入通过：账号/成绩已入 SQLite，明文登录后迁移为哈希")
    return 0


if __name__ == "__main__":
    sys.exit(main())
