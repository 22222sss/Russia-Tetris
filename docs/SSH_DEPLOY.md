# SSH 承载：把服务端做成"能上公网"的形态

> 面向部署者。目标只有一句话：**公网只开 sshd，9999 只留给本机**。
> 加密、认证、账号隔离都交给 SSH，游戏协议一行都不用改。

## 1. 为什么是这个方案

现在的服务端是裸 TCP：密码、成绩、全部按键都在网线上明文走。上线前必须解决。

| 方案 | 代价 | 结论 |
|---|---|---|
| **SSH 承载**（本方案） | sshd 里加一个 `Match` 块 + 一个小桥程序；协议零改动 | ✅ 采用 |
| 自己做 TLS（OpenSSL 层） | 要新写握手、证书、客户端也要改；协议不再是"telnet 就能连" | ❌ 成本远大于收益 |
| `stunnel` / `socat` 包一层 | 能加密，但玩家侧仍要自己的客户端；本地回显和回车翻译还得另做 | ❌ 只解决一半 |

服务端原本就是"渲染终端"的程序（ANSI 全屏 + 方向键），而这正是 SSH 会话天然提供的东西，
所以把会话的 stdin/stdout 接到服务端，比在应用层加加密简单得多。

## 2. 架构：谁跑什么

```
玩家                                    服务器
  │  ssh tetris@host                        │
  │  （sshd 认证 + 加密）                    │
  ▼                                         ▼
sshd ──ForceCommand──► tetris-bridge ──TCP──► tetris_game ──► tetris.db
（SSH 服务）           （每会话一个进程）  127.0.0.1:9999     （SQLite，WAL）
```

- **sshd**：唯一的公网入口。`Match User tetris` 让这个账号一登录就执行 `ForceCommand`，
  玩家拿不到 shell；
- **tetris-bridge**（`deploy/tetris-bridge.py`）：把会话的 stdin/stdout 和本机 9999 对接，
  顺便补上终端该做的事（见第 5 节）；
- **tetris_game**：一行没改，仍然只讲原来的协议，只是现在只绑 `127.0.0.1`；
- **两个账号别混用**：SSH 登录账号（如 `tetris`）只跑桥；服务端建议用另一个账号（如 `tetrisd`）
  从它自己的目录启动，这样登录账号读不到 `tetris.db`（库里有密码哈希）。

## 3. 安装

```bash
# 1) 服务端：编译并按"只绑回环"启动（TETRIS_BIND 见第 4 节）
cd src && make
TETRIS_BIND=127.0.0.1 ./tetris_game

# 2) SSH 入口：装桥 + sshd 片段 + 登录账号（幂等，sshd 校验失败会自动还原）
sudo deploy/install-ssh-bridge.sh
#    没有 TETRIS_LOGIN_PASSWORD 时脚本不会替你编一个口令，需要手动：
sudo passwd tetris
```

装完的效果：

- `/usr/local/bin/tetris-bridge`（0755，root 所有）；
- `/etc/ssh/sshd_config.d/tetris.conf`（本仓库 `deploy/sshd-tetris.conf` 的副本）；
- 系统账号 `tetris`，shell 为 `/bin/sh`（**不能是 nologin**，原因见排错表），口令由你设置。

> 手工装也可以：`install -m 0755 deploy/tetris-bridge.py /usr/local/bin/tetris-bridge`，
> 把 `deploy/sshd-tetris.conf` 放进 `sshd_config.d/`，`sshd -t && systemctl reload ssh`。
> 前提是主配置里有 `Include /etc/ssh/sshd_config.d/*.conf`（Ubuntu 22.04 默认有）。

## 4. 服务端侧：`TETRIS_BIND`

| 变量 | 默认 | 作用 |
|---|---|---|
| `TETRIS_BIND` | `0.0.0.0` | 监听地址。走 SSH 承载时设 `127.0.0.1` |

不设它的话，任何人都能绕过 sshd 直接 `telnet host 9999`，明文密码照样过网 ——
SSH 承载就白做了。设完之后自检：

```bash
ss -ltnp | grep 9999        # 期望：127.0.0.1:9999，不是 0.0.0.0:9999 或 *:9999
```

## 5. 桥做了什么

| 终端行为 | 不做会怎样 | 桥的做法 |
|---|---|---|
| **raw 模式** | canonical 模式下方向键要按回车才发出去（就是 `telnet` 不能用那个坑） | 连上服务端后 `termios.setraw(stdin)`，退出时恢复 |
| **回车翻译** | raw 终端只发 CR（`0x0D`），而服务端按 `\r\n` 分帧 → 所有菜单都卡住 | 把 CR 补成 CRLF（已是 CRLF 的不重复补） |
| **本地回显** | 服务端不回显、raw 又关了 pty 的 echo → 输入用户名时看不见自己打的字 | 只回显可打印字符（含 UTF-8 多字节）；含 ESC 的整块不回显，否则方向键会把屏幕弄乱 |
| **半关闭** | 会话结束时服务端收不到 EOF，`User` 对象要等 100ms 清理/连接超时 | stdin EOF → `shutdown(SHUT_WR)`，再把服务端最后那屏收完 |
| **stdout 纯净** | 桥的任何日志都会写进玩家屏幕（sshd 把 stderr 也接到同一个 pty） | 诊断只走 syslog / 文件，永不写 stdout/stderr |

环境变量（命令行参数优先，见 `tetris-bridge --help`）：

| 变量 | 默认 | 作用 |
|---|---|---|
| `TETRIS_BRIDGE_TARGET` | `127.0.0.1:9999` | 服务端地址 |
| `TETRIS_BRIDGE_CONNECT_TIMEOUT` | `3` | 连不上服务端时，几秒后放弃并给提示 |
| `TETRIS_BRIDGE_SOURCE_IP` | `hash` | 来源地址映射；`default` 表示固定 `127.0.0.1`（见第 6 节） |
| `TETRIS_BRIDGE_ECHO` | `auto` | `auto`=仅当 stdin/stdout 都是终端时回显；`on`/`off` 强制 |
| `TETRIS_BRIDGE_LOG` | 空 | 诊断日志文件；不设就走 syslog（没有 `/dev/log` 时静默） |

## 6. 来源 IP 与"按 IP 限流"

服务端的第三层认证加固是**按来源 IP** 限流（默认 300 次失败 / 5 分钟，`IpThrottle`）。
桥接之后所有会话都来自 `127.0.0.1`，这个额度会退化成**全站共享一份** ——
几十个玩家正常打错密码就能把所有人一起锁住。

桥因此把真实客户端地址映射成一个**该客户端专属**的环回源地址：

```
真实客户端 203.0.113.7  ->  源地址 127.0.113.7   （IPv4 保留后三字节）
真实客户端 198.51.100.9 ->  源地址 127.51.100.9
IPv6 客户端              ->  127.255.<hash0>.<hash1>
```

于是：

- 同一个玩家恒定同一地址（跨会话一致），**不同玩家互不吃额度**；
- 服务端日志里看到的是 `127.113.7` 这种伪匿名标识，不等于真实 IP，
  但"按玩家区分"这件事还在（`TETRIS_LOG_LEVEL=info` 时日志里是
  `Client[5] 连接! 来自 127.0.0.113.7`）；
- 映射失败时（目标不是本机回环、平台不允许 bind `127/8`、`--source-ip default`）
  退回 `127.0.0.1`，日志记一行 —— 那时请把 `TETRIS_AUTH_IP_MAX_FAILURES` 当成
  **全站额度**来配（例如按"峰值并发 × 每人几次输错"估，或直接调大）。

`127.0.0.1` / `127.0.0.53`（systemd-resolved）/ `127.0.0.11`（docker DNS）这几个本机
保留地址不会被映射占掉；客户端本身是 `127/8`（本机测试）时也走默认源。

## 7. 玩家怎么进

```bash
ssh tetris@<服务器 IP>          # 密码就是管理员设的那个共享口令
ssh -p 2222 tetris@<IP>         # 非默认端口
```

- 进去直接是游戏主菜单。**Ctrl+C 退出**（桥会关掉连接并恢复终端）；
- 终端窗口至少 **80×24**（游戏区 24 行，菜单最右到第 50 列左右），更小的窗口会折行乱掉；
- 终端要能显示 UTF-8（Windows 用 Windows Terminal 或先 `chcp 65001`）；
- 不需要 `telnet`，也不需要 `tetris_client.py` —— 那两样是"裸 TCP 时代"的客户端；
- 玩家看到的身份仍然是**游戏里的账号/密码**：SSH 账号是共享的入口，不是玩家身份。

## 8. 安全边界

- **`ForceCommand` 不是沙箱**：它只保证登录后跑不了别的命令，不限制进程能看到什么。
  所以登录账号本身要尽量穷（不要给它读服务端目录/数据库的权限）；
- sshd 片段里已关掉 `AllowTcpForwarding` / `AllowAgentForwarding` /
  `AllowStreamLocalForwarding` / `X11Forwarding` / `PermitTunnel` / `PermitUserRC`，
  并限制 `MaxSessions 1`（防止 ControlMaster 复用出第二条游戏会话）；
  注意 `PermitUserEnvironment` **不能在 Match 块里写**（sshd 会拒绝加载整个片段），
  它只能在全局设，Ubuntu/Debian 默认就是 `no`；
- 共享口令意味着"知道口令就能进到登录界面"，真正的门是游戏账号密码 + 三层限流；
  建议在 sshd 全局开 `MaxAuthTries 3`、`LoginGraceTime 20`，公网机器再挂 fail2ban 之类；
- 想彻底不要共享口令，可以换成"每个玩家一个 SSH key"（`AuthorizedKeysCommand` 由游戏侧签发），
  那是后续可选路线，本轮没做；
- 桥以登录账号身份运行，只能连 `127.0.0.1:9999`；它不写数据库、不读数据库。

## 9. 验证清单

```bash
# 桥自身：字节透传 / CRLF / 回显 / EOF / 退出码 / 来源地址映射 / pty raw（Linux）
python3 tests/bridge_test.py

# 穿过桥跑一遍真协议（需要服务端在跑）
python3 tests/bridge_test.py --server 127.0.0.1:9999 --db tetris.db

# sshd 配置语法
sudo sshd -t

# 端到端（从另一台机器）
ssh tetris@<IP>          # 应直接进主菜单；方向键立刻生效；Ctrl+C 退出

# 明文端口不该再暴露
ss -ltnp | grep 9999     # 127.0.0.1:9999
```

CI 里对应两步：`SSH bridge test`（桥的 8 项自检 + 穿过桥的真协议 + `TETRIS_BIND`
只绑回环的断言）和 `Verify sshd snippet`（`sshd -t` 解析片段）。

## 10. 排错表

| 现象 | 原因 | 处理 |
|---|---|---|
| 连上就断，提示 `This account is currently not available.` | 登录账号的 shell 是 `nologin`/`false` —— sshd 是用"用户 shell -c ForceCommand"执行的，nologin 直接把它拒了 | `chsh -s /bin/sh tetris`（安装脚本会自动改） |
| 能看到菜单，但按回车没反应 | 会话没分到 pty（`PermitTTY no`，或客户端用了 `ssh -T`） | 用 `ssh tetris@host`（不加 `-T`），片段里保留 `PermitTTY yes` |
| 方向键要按回车才生效 | 桥没起来（`ForceCommand` 路径写错），走的是 shell 的 canonical 模式 | `ls -l /usr/local/bin/tetris-bridge`；`sshd -T -C user=tetris` 看生效的 `forcecommand` |
| 满屏重复字符 / 打一个字出两个 | pty 的 ECHO 没关（内核回显 + 桥回显各一份） | 确认跑的是本仓库的桥（它会 `setraw`），不要用 `socat`/`nc` 顶替 |
| 登录时看不见自己输入 | `--echo off`，或 stdout 不是终端（`ssh ... > file`） | 保持 `TETRIS_BRIDGE_ECHO=auto` |
| 屏幕折行 / 错位 | 终端小于 80×24 | 放大窗口再重进 |
| 提示"游戏服务暂时不可用（无法连接游戏服务器 127.0.0.1:9999）" | 服务端没起 / 没绑在回环 / 端口不对 | 起服务端，或改 `TETRIS_BRIDGE_TARGET` |
| 所有人都被"登录尝试过于频繁"挡住 | 来源地址映射没生效，全站共用一份 IP 额度 | 用 `TETRIS_BRIDGE_LOG` 看是否记了"bind 源地址失败"；按第 6 节调 `TETRIS_AUTH_IP_MAX_FAILURES` |
| `sshd` 起不来 / reload 失败 | 片段里 `Match` 没有用 `Match all` 收尾，把后面的全局选项吞了；或用了该版本不支持的选项 | `sshd -t` 看报错；片段末尾必须有 `Match all` |

## 11. 本轮没做的（下一步）

- **每会话内存**：桥是 Python 解释器 + 标准库，每会话一个进程，**CI 实测 RSS 16.8 MB**
  （`tests/bridge_test.py` 在 Linux 上会把这个数打成 `::notice::`）。
  200 个并发玩家光桥就是 ~3.4 GB，再加每会话一个 sshd 会话进程 —— **SSH 承载的并发
  天花板是内存**，适合几十人的规模。要压更多并发，值得把桥改成一个几十行的 C 程序
  （预期 1~2 MB/会话）；服务端本身不用再优化（1 万按键/秒只用 0.1~0.15 核）；
- **不用共享口令**：`AuthorizedKeysCommand` + 游戏侧签发 key，让每个玩家一条独立 SSH 身份；
- 管理员 CLI / 备份、Docker / systemd 单元（见 `docs/HANDOVER.md` 第 8 节）。
