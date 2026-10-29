"""最小协议 mock：用来在本地验证 tests/integration_test.py 的断言逻辑。

只实现集成测试会走到的分支，行为按 Russia-Tetris 服务端源码的文本拼出来。
"""
import socket
import sys
import threading

PORT = int(sys.argv[1])
USERDATA = sys.argv[2]

MENU = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请选择操作："
        "\x1b[13;14H\x1b[37m1. 注册帐号"
        "\x1b[14;14H\x1b[37m2. 登录"
        "\x1b[16;14H\x1b[37m请选择操作：")
LOAD_MENU = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请选择操作："
             "\x1b[14;14H\x1b[37m1. 我最近的成绩"
             "\x1b[16;14H\x1b[37m2. 全服top成绩"
             "\x1b[18;14H\x1b[37m3. 开始游戏"
             "\x1b[20;14H\x1b[37m4. 返回服务"
             "\x1b[24;14H\x1b[37m你的选择是：")
DIFF = ("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m1. 简单模式"
        "\x1b[14;14H\x1b[37m2. 普通模式"
        "\x1b[16;14H\x1b[37m3. 困难模式"
        "\x1b[18;14H\x1b[37m4. 返回上级菜单"
        "\x1b[20;14H\x1b[37m请选择操作：")
GAME_ON = "\x1b[2J\x1b[H\x1b[2;41H\x1b[37mNext:\x1b[14;42H\x1b[37mScore: "
OVER = "\x1b[12;14H\x1b[37mGAME OVER\r\n\x1b[15;14H\x1b[37mStart Again ? (y/n):"

users = {}
lock = threading.Lock()


def write_csv(name, password):
    try:
        empty = False
        try:
            with open(USERDATA, "r", encoding="utf-8") as fh:
                empty = fh.read(1) == ""
        except FileNotFoundError:
            empty = True
        with open(USERDATA, "a", encoding="utf-8") as fh:
            if empty:
                fh.write("Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
                         "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
                         "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores\n")
            fh.write("%s,%s,0,EASY,,0,NORMAL,,0,DIFFICULT,,\n" % (name, password))
    except OSError as exc:
        print("csv error:", exc)
        sys.exit(1)


def serve(conn):
    conn.sendall(MENU.encode())
    state = "menu"
    buf = b""
    downs = 0
    try:
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                return
            buf += chunk

            if state in ("playing", "over"):
                if state == "playing":
                    downs += chunk.count(b"\x1b[B")
                    if downs >= 30:
                        conn.sendall(OVER.encode())
                        state = "over"
                else:
                    if b"n" in chunk or b"N" in chunk:
                        conn.sendall(MENU.encode())
                        state = "menu"
                continue

            while b"\r\n" in buf:
                line, buf = buf.split(b"\r\n", 1)
                cmd = line.decode("utf-8", "replace")
                if state == "menu":
                    if cmd == "1":
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请输入用户名：".encode())
                        state = "reg_user"
                    elif cmd == "2":
                        conn.sendall("\x1b[2J\x1b[H\x1b[12;14H\x1b[37m请输入用户名：".encode())
                        state = "login_user"
                    else:
                        conn.sendall(MENU.encode())
                elif state == "reg_user":
                    if cmd in users:
                        conn.sendall("该用户名已被注册，请选择其他用户名:".encode())
                    else:
                        with lock:
                            users[cmd] = None
                        conn.sendall("\x1b[15;14H\x1b[37m请输入密码：".encode())
                        state = "reg_pwd"
                        pending_user = cmd
                elif state == "reg_pwd":
                    with lock:
                        users[pending_user] = cmd
                    write_csv(pending_user, cmd)
                    conn.sendall("\x1b[16;14H\x1b[37m注册成功！\r\n"
                                 "\x1b[17;14H\x1b[37m请按3返回上级菜单：".encode())
                    state = "after_reg"
                elif state == "after_reg":
                    conn.sendall(MENU.encode())
                    state = "menu"
                elif state == "login_user":
                    pending_user = cmd
                    conn.sendall("\x1b[15;14H\x1b[37m请输入密码：".encode())
                    state = "login_pwd"
                elif state == "login_pwd":
                    if users.get(pending_user) == cmd:
                        conn.sendall("登录中...\r\n".encode())
                        conn.sendall(LOAD_MENU.encode())
                        state = "load_menu"
                    else:
                        conn.sendall("登录失败，用户名或密码错误。\r\n按3返回上级菜单:".encode())
                        state = "after_fail"
                elif state == "after_fail":
                    conn.sendall(MENU.encode())
                    state = "menu"
                elif state == "load_menu":
                    if cmd == "1":
                        conn.sendall("暂无成绩记录".encode())
                        state = "score_view"
                    elif cmd == "2":
                        conn.sendall("简单模式：\r\n用户名 分数 用户获取最高分的时间\r\n"
                                     "请按3返回菜单：".encode())
                        state = "top_view"
                    elif cmd == "3":
                        conn.sendall(DIFF.encode())
                        state = "diff"
                    else:
                        conn.sendall(MENU.encode())
                        state = "menu"
                elif state in ("score_view", "top_view"):
                    conn.sendall(LOAD_MENU.encode())
                    state = "load_menu"
                elif state == "diff":
                    if cmd in ("1", "2", "3"):
                        conn.sendall(GAME_ON.encode())
                        state = "playing"
                        downs = 0
                    else:
                        conn.sendall(LOAD_MENU.encode())
                        state = "load_menu"
    except OSError:
        return
    finally:
        try:
            conn.close()
        except OSError:
            pass


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(64)
    print("mock listening on", PORT, flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
