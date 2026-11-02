"""测试侧的 SQLite 存取（schema 必须与 src/TetrisGame/Db.cpp 保持一致）。

只依赖标准库 sqlite3。mock_server 与各个测试脚本共用这一份，避免 schema/字段
在多个脚本里各抄一遍、慢慢走样。

注意：这里**不**导入服务端的 C++ 实现；两边只共享"表结构 + 字段含义"这层契约，
和以前 mock 与真服务端共享 userdata.csv 格式是同一个思路。
"""

import sqlite3
import time

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS accounts (
    username   TEXT PRIMARY KEY,
    password   TEXT NOT NULL,
    created_at INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS scores (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    username   TEXT NOT NULL,
    difficulty TEXT NOT NULL,
    score      INTEGER NOT NULL,
    played_at  TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_scores_user ON scores(username, id DESC);
CREATE INDEX IF NOT EXISTS idx_scores_best ON scores(username, difficulty, score DESC, id ASC);
CREATE INDEX IF NOT EXISTS idx_scores_top  ON scores(difficulty, score DESC, id ASC);
"""


def connect(path):
    """打开（必要时创建）数据库；WAL + busy_timeout 与服务端一致。"""
    con = sqlite3.connect(path, timeout=10.0, isolation_level=None)
    con.execute("PRAGMA busy_timeout=5000")
    con.executescript(SCHEMA)
    return con


def read_credential(path, username):
    """读某账号的 password 字段；账号不存在（或库还没建好）返回 None。"""
    if not username:
        return None
    try:
        con = sqlite3.connect(path, timeout=10.0)
        try:
            row = con.execute(
                "SELECT password FROM accounts WHERE username=?", (username,)).fetchone()
        finally:
            con.close()
    except sqlite3.Error:
        return None
    return row[0] if row else None


def account_exists(path, username):
    return read_credential(path, username) is not None


def wait_for_account(path, username, timeout=15.0):
    """轮询到账号出现（注册是异步落库的）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if read_credential(path, username) is not None:
            return True
        time.sleep(0.2)
    return False


def wait_for_credential(path, username, predicate, timeout=15.0):
    """轮询到 password 字段满足 predicate；超时返回 None。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        stored = read_credential(path, username)
        if stored is not None and predicate(stored):
            return stored
        time.sleep(0.2)
    return None


def insert_account(path, username, password_or_hash):
    """直接落库（用于 --seed 压测账号、以及造"旧明文账号"）。"""
    con = connect(path)
    try:
        con.execute(
            "INSERT OR REPLACE INTO accounts(username,password,created_at) VALUES(?,?,?)",
            (username, password_or_hash, int(time.time())))
    finally:
        con.close()


def seed_accounts(path, rows):
    """批量落库：rows = [(username, password_or_hash), ...]，压测前一次写完。"""
    now = int(time.time())
    con = connect(path)
    try:
        con.executemany(
            "INSERT OR REPLACE INTO accounts(username,password,created_at) VALUES(?,?,?)",
            [(username, credential, now) for username, credential in rows])
    finally:
        con.close()


def update_credential(path, username, password_or_hash):
    con = connect(path)
    try:
        con.execute("UPDATE accounts SET password=? WHERE username=?",
                    (password_or_hash, username))
    finally:
        con.close()


def insert_score(path, username, difficulty, score, played_at=""):
    con = connect(path)
    try:
        con.execute(
            "INSERT INTO scores(username,difficulty,score,played_at) VALUES(?,?,?,?)",
            (username, difficulty, score, played_at))
    finally:
        con.close()


def count_accounts(path):
    try:
        con = sqlite3.connect(path, timeout=10.0)
        try:
            return con.execute("SELECT COUNT(*) FROM accounts").fetchone()[0]
        finally:
            con.close()
    except sqlite3.Error:
        return 0


def scores_for(path, username):
    """返回某账号的成绩 [(score, difficulty), ...]（最新的在前）。"""
    try:
        con = sqlite3.connect(path, timeout=10.0)
        try:
            return con.execute(
                "SELECT score, difficulty FROM scores WHERE username=? ORDER BY id DESC",
                (username,)).fetchall()
        finally:
            con.close()
    except sqlite3.Error:
        return []


def wait_for_gone(path, username, timeout=15.0):
    """轮询到账号消失（重置/删除类用例用）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if read_credential(path, username) is None:
            return True
        time.sleep(0.2)
    return False
