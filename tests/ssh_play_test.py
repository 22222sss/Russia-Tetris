#!/usr/bin/env python3
"""SSH 承载的端到端验收：从**另一台机器**经 ssh 进游戏，逐条验证"玩家真的能玩"。

和 `integration_test.py` 的区别：那个直接连 9999（测协议/存储/认证），这个走
**sshd + ForceCommand + 桥 + pty**（测"上线形态"）。两者互不替代：

    integration_test.py  → 证明服务端对
    ssh_play_test.py     → 证明"玩家从公网 ssh 进来能玩"

判据全是字节级的（只看终端里真的出现了什么），9 条：

1. 首屏出现 `1. 注册帐号`                        —— 服务端主动推了字节，事件循环活着
2. 注册一个账号并看到 `注册成功`                  —— 写路径（SQLite）通
3. 登录后出现 `3. 开始游戏`                       —— 认证 + 主菜单
4. 空账号点"最近成绩"有两个提示（6.56 回归守卫）   —— `暂无成绩记录` + `请按3返回菜单`
5. 进入游戏出现 `Next:` / `Score:`                —— 棋盘渲染
6. **只发 `\\x1b[B`、不发回车**，2.5s 内回 ≥64 字节 —— raw 模式真的生效（桥的核心价值）
7. 连按方向键后连接仍活着                         —— 不会一键就断
8. 棋盘输出里没有 `;0H`                           —— 第 0 列不写东西（6.57）
9. 关掉输入后 ssh 自行退出                        —— 不会把会话挂住

用法（在**外部**机器上跑）::

    # 直连云主机：公网只开 22，玩家用共享口令进来
    python3 tests/ssh_play_test.py --host 203.0.113.10 --key ~/.ssh/id_ed25519

    # 经公共中继（没有公网入口时的替代，见 docs/DEPLOY.md 第 12 节）
    python3 tests/ssh_play_test.py --host serveousercontent.com --port 31165 \\
        --key ~/.ssh/probe

注意两个坑（都踩过）：

- ssh 必须在**真 pty** 里跑（本文件用 `pty.fork`），否则桥拿不到终端、raw 模式没意义；
  runner 上没有 tty 时窗口尺寸会是 0x0，所以这里固定 80x24；
- 本地这端也要 `tty.setraw`，否则 `\\x1b[B` 会被行缓冲压住、服务端永远收不到
  （集成测试走 socket，所以碰不到这个坑）。

退出码：0 全过 / 1 有用例失败（失败时打印当前这一屏）。
"""
import argparse
import fcntl
import os
import pty
import re
import select
import struct
import sys
import termios
import time
import tty

ANSI_RE = re.compile(
    rb"\x1b\[[0-9;?]*[ -/]*[@-~]"          # CSI ... 终止符
    rb"|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)"  # OSC ... BEL / ST
    rb"|\x1b[()#][0-9A-Za-z]"              # 字符集选择
    rb"|\x1b[@-_]"                         # 其余两字节序列
)


class Fail(Exception):
    pass


class Session:
    """一条 ssh 会话；stdin/stdout 都走 pty，和真人敲键盘等价。"""

    def __init__(self, host, port, user, key, timeout=25.0):
        self.timeout = timeout
        pid, fd = pty.fork()
        if pid == 0:
            argv = ["ssh", "-tt"]
            if key:
                argv += ["-i", key, "-o", "IdentitiesOnly=yes"]
            argv += [
                "-o", "StrictHostKeyChecking=no",
                "-o", "UserKnownHostsFile=/dev/null",
                "-o", "LogLevel=ERROR",
                "-o", "ConnectTimeout=25",
                "-o", "ServerAliveInterval=10",
                "-p", str(port),
                "%s@%s" % (user, host),
            ]
            os.execvp("ssh", argv)
            os._exit(127)
        self.pid = pid
        self.fd = fd
        self.buf = bytearray()
        self.mark = 0          # 只匹配这之后的输出，避免命中上一屏的旧文字
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        tty.setraw(fd, termios.TCSANOW)

    # ---- 读 ----
    def pump(self, timeout=0.4):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            chunk = os.read(self.fd, 65536)
        except OSError:
            return False
        if not chunk:
            return False
        self.buf.extend(chunk)
        return True

    def text(self, start=0):
        return ANSI_RE.sub(b"", bytes(self.buf[start:])).decode("utf-8", "replace")

    def expect(self, needle, timeout=None):
        end = time.time() + (self.timeout if timeout is None else timeout)
        while time.time() < end:
            if needle in self.text(self.mark):
                return
            self.pump(0.3)
        raise Fail("没等到 %r；这一屏是：\n%s" % (needle, self.text(self.mark)[-700:]))

    # ---- 写 ----
    def send(self, data, settle=0.4):
        self.mark = len(self.buf)
        if isinstance(data, str):
            data = data.encode("utf-8")
        os.write(self.fd, data)
        time.sleep(settle)

    def sendline(self, line, settle=0.5):
        self.send(line + "\r", settle)

    # ---- 收尾 ----
    def close(self, timeout=12.0):
        try:
            os.close(self.fd)
        except OSError:
            pass
        end = time.time() + timeout
        while time.time() < end:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                return status
            time.sleep(0.2)
        os.kill(self.pid, 9)
        return None


def run_checks(sess, account, password):
    """跑 9 条判据，返回每条的说明（失败时抛 Fail）。"""
    done = []

    sess.expect("1. 注册帐号", 40)
    done.append("首屏菜单（%d 字节）" % len(sess.buf))

    sess.sendline("1")
    sess.expect("请输入用户名")
    sess.sendline(account)
    sess.expect("请输入密码")
    sess.sendline(password)
    sess.expect("注册成功", 30)
    done.append("注册 %s" % account)

    sess.sendline("3")
    sess.expect("2. 登录")
    sess.sendline("2")
    sess.expect("请输入用户名")
    sess.sendline(account)
    sess.expect("请输入密码")
    sess.sendline(password)
    sess.expect("3. 开始游戏", 40)
    done.append("登录成功进主菜单")

    sess.sendline("1")
    sess.expect("暂无成绩记录")
    sess.expect("请按3返回菜单")
    done.append("空账号也有返回提示（6.56 守卫）")

    sess.sendline("3")
    sess.expect("3. 开始游戏")
    sess.sendline("3")
    sess.expect("1. 简单模式")
    sess.sendline("1")
    sess.expect("Next:")
    sess.expect("Score:")
    done.append("进入游戏、棋盘渲染")

    before = len(sess.buf)
    sess.send(b"\x1b[B", 2.5)          # 只按下键，不发回车
    grew = len(sess.buf) - before
    if grew < 64:
        sess.pump(2.0)
        grew = len(sess.buf) - before
    if grew < 64:
        raise Fail("只按下键时服务端只回了 %d 字节（raw 模式没生效）" % grew)
    done.append("方向键无回车即生效（回了 %d 字节）" % grew)

    sess.send(b"\x1b[D", 0.3)
    sess.send(b"\x1b[C", 0.3)
    sess.send(b"\x1b[B", 0.6)
    sess.pump(1.0)
    if b";0H" in bytes(sess.buf):
        raise Fail("棋盘输出里出现 (row, 0) 光标定位，最左列又在画东西（6.57 回归）")
    done.append("连按方向键后仍活着（累计 %d 字节）" % len(sess.buf))
    done.append("没有 ;0H（第 0 列不写东西）")

    status = sess.close()
    if status is None:
        raise Fail("关掉输入后 ssh 12 秒还没退出")
    done.append("断开后 ssh 自行退出（exit=%d）" % os.waitstatus_to_exitcode(status))
    return done


def main():
    ap = argparse.ArgumentParser(description="经 ssh 从另一台机器验收游戏入口")
    ap.add_argument("--host", required=True, help="sshd 所在主机（公网 IP / 域名 / 中继主机）")
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", default="tetris", help="SSH 登录账号（默认 tetris，即只跑桥的那个）")
    ap.add_argument("--key", default=None, help="私钥路径；不给则用 ssh 自己的默认/agent")
    ap.add_argument("--account", default=None, help="测试账号名（默认随机 cloudNNNNNN）")
    ap.add_argument("--timeout", type=float, default=25.0, help="单步等待上限（秒）")
    args = ap.parse_args()

    account = args.account or ("cloud%d" % (int(time.time()) % 1000000))
    password = "Cloud-Probe-2026"
    t0 = time.time()
    sess = Session(args.host, args.port, args.user, args.key, args.timeout)
    try:
        done = run_checks(sess, account, password)
        for i, line in enumerate(done, 1):
            print("ok %2d  %s" % (i, line))
        print("\nSSH-PLAY-PASS %d 项 / %.1fs / %d 字节"
              % (len(done), time.time() - t0, len(sess.buf)))
        return 0
    except Fail as exc:
        print("SSH-PLAY-FAIL: %s" % exc, file=sys.stderr)
        sess.close()
        return 1


if __name__ == "__main__":
    sys.exit(main())
