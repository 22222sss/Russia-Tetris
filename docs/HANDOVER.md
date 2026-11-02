## 0. 当前状态（截至 2026-11-02）

> 本次会话（2026-11-02）七轮：**① 按来源 IP 兜底** → **② 改密/重置** → **③ 连接池 trim**
> → **④ 存储层 CSV → SQLite** → **⑤ SSH 承载** → **⑥ 管理员 CLI + 备份** →
> **⑦ 打包/部署（Dockerfile + systemd 单元 + 日志轮转）**。
> 更早：`-O2`（4.7）→ `User` 棋盘 int8（4.8）→ 连接池预创建 2048（4.9）→ 认证加固第一/二批。
> 每轮实现细节见第 5 节，性能数据见第 4 节。

**本轮做完了打包/部署：镜像 + systemd 单元 + 日志轮转**（`Dockerfile` / `deploy/systemd/` /
`main.cpp` 的 `TETRIS_LOG_*`）

1. **入口脚本而不是文档里的十几条命令**：`deploy/install-systemd.sh` 幂等完成
   （装二进制/备份脚本 → 建 `tetrisd` 账号 → 建三个目录 → 装三个单元 → 装 sysctl →
   开机自启 → 启动并自检）；`deploy/install-ssh-bridge.sh` 仍是 SSH 侧那一步；
2. **`LimitNOFILE=300000:300000`**（软硬都写）：systemd 默认 nofile 是 `1024:524288`，
   而 `SystemOptimizer` 只会把软限制抬到硬限制 —— 硬限制本身不够时它就是一条看不见的
   连接天花板。CI 里既查 `systemctl show -p LimitNOFILE`，也读 `/proc/<pid>/limits` 核对进程；
3. **日志真的会轮转了**：`main.cpp` 从 `basic_logger_mt` 换成 `rotating_logger_mt`，
   新增 `TETRIS_LOG_DIR`（日志目录，默认当前目录）、`TETRIS_LOG_MAX_BYTES`（默认 10 MiB，0=不轮转）、
   `TETRIS_LOG_MAX_FILES`（默认 5 份历史，当前文件另算）；**没有用外部 logrotate**（spdlog 的 fd 常开，
   `create` 会让进程继续写旧 inode、`copytruncate` 会在文件里留 NUL 空洞，见 6.44）；
   打不开日志目录时退回 stderr 并提示，不让"日志写不了"变成"服务起不来"；
4. **SIGTERM 处理器**（单 worker 模式）：容器里 `tetris_game` 通常是 1 号进程，而内核不会把
   "默认动作是终止"的信号投递给 1 号进程 —— 不装处理器的话 `docker stop` 会干等到超时再
   SIGKILL、systemd stop 也会拖到 `TimeoutStopSec`；现在收到就 `_exit(0)`（WAL 崩溃安全，见 6.46）；
5. **每日备份变成系统的一部分**：`tetris-backup.timer`（每天 04:15 + 随机延迟 + `Persistent`）
   → `tetris-backup.service` → `deploy/tetris-backup.sh` → `tetris-admin backup`；
   保留 14 份、失败删半成品并让 systemd 记 `failed`；备份目录 `/var/backups/tetris` 故意
   不在库所在目录（6.41）；
6. **Dockerfile**：`debian:bookworm`（构建）→ `debian:bookworm-slim`（运行），只编一次、
   `strip` 过、运行阶段只装运行库、`USER tetris`（uid 10001）、`VOLUME /data`、
   `HEALTHCHECK` 真的连 `127.0.0.1:9999` 并**读到首屏字节**才算健康；
   容器里 `TETRIS_BIND=0.0.0.0` + 宿主 `-p 127.0.0.1:9999:9999`（原因见 6.47），
   `docker-compose.yml` 用 host 网络回到"绑 127.0.0.1"的形态；
7. **测试/CI**：新增 `tests/deploy_test.py`（10 项静态断言：单元关键项、环境变量拼写、
   Dockerfile、.dockerignore、compose、脚本语法、备份脚本行为、文档不漂移）与
   `tests/log_rotation_test.py`（真二进制，500 条连接逼出多次轮转，断言 `log.N.txt` 出现、
   份数/大小不超限、日志落在 `TETRIS_LOG_DIR`）；CI 末尾新增「Deploy artifacts test
   (systemd)」（**真的装一遍**：LimitNOFILE/只绑回环/真跑一次备份 + 权限 600 + integrity_check/
   `kill -9` 后等到**新 MainPID**/stop 计时）与「Docker image build and health check」
   （真 build、真跑、断言 healthy/非 root/fd 300000/宿主能读到字节/容器内 tetris-admin 可用）。
   job 从 22 步变 24 步，`timeout-minutes` 15 → 30；
8. **上线 checklist 落在 `docs/DEPLOY.md`**：准备 → 部署 → 验证 → 运维四段，每条都带
   "怎么验"（`ss -ltn` 只绑回环、`/proc/<pid>/limits`、`systemctl --failed`、恢复演练……），
   另有升级/回滚、日志、备份恢复、排错表、安全边界各一节。

> **CI 实测（SHA `c23d1e1`，一轮全绿）**：`deploy_test` 9/9（之后加入恢复演练工具，变成 10 项）；日志轮转真的轮了
> （`TETRIS_LOG_MAX_BYTES=4096` × 3 份历史 → `log.1/2/3.txt` + `log.txt`，合计 15 KB）；
> `install-systemd.sh` 真的装成功：`/proc/<pid>/limits` = 300000/300000、`systemctl show`
> 的两个 nofile 属性都是 300000、`ss -ltn` 只有 127.0.0.1:9999、`tetris-backup` 跑出
> 40 KB / 600 / integrity ok、`kill -9` 后从 pid 3595 拉到 3737、stop <5s；
> 镜像 build 出来 healthy + uid 10001 + 宿主能读到 9999 + 容器内 `tetris-admin stats` 可用；
> 其余 22 步照旧通过。**本轮被 CI 抓到的四个错**（本地都没有 g++/systemd/docker/bash，只能靠它）：
> spdlog 头文件路径（6.50）、`LimitNOFILE` 被 systemd 拆成软/硬两个属性（6.45）、
> spdlog 的 `max_files` 是"历史份数"而不是总数、`/var/backups/tetris` 是 0700 导致 CI 用
> runner 用户读不到（检查必须 `sudo`）。

**上一轮做完了管理命令行：`tetris-admin`**（`src/tetris_admin.cpp`）

1. **为什么要它**：没有邮箱，"忘记密码"就没有自救路径（6.28），以前只能手工 `sqlite3` 改库；
   备份也一直靠记命令。现在 `make` 会一次编出 `tetris_game` 和 `tetris-admin` 两个二进制，
   后者只链 `Db` / `PasswordHash`，不拖网络与游戏逻辑；
2. **子命令**：`reset-password`（交互输两次 / `--password-stdin`）、`backup`（Online Backup API）、
   `list-users`（带"还是旧明文"标记）、`stats`；退出码 `0` 成功 / `1` 内部错误 /
   `2` 用法错误或账号不存在 / `3` 新密码不满足策略；
3. **不另造第三套格式**：新密码走同一套 `PasswordPolicy` + `PasswordHash`，落盘一条
   `UPDATE accounts.password` —— 和用户在主菜单 5 改密完全等价；**服务端不用停**
   （`Db::cacheStamp()` 内含 SQLite `data_version`，别的进程立刻能看到）；也不踢已登录会话；
4. **备份**：SQLite **Online Backup API**（不依赖外部 `sqlite3`），WAL 下运行中也能拿到一致
   快照；完成后跑 `integrity_check` + 统计条数 + `chmod 600`（里面是密码哈希）；
   备份期间库里有写入只在输出里说明，**不算失败**；
5. **启动打印生效配置**（`main.cpp::printEffectiveConfig`）：fork 之前打一次，二十来个环境
   变量一目了然；值全部取自各模块自己的 `configFromEnv()`/访问器，**不另抄默认值**（6.42）；
6. **测试/CI**：新增 `tests/admin_test.py`（**10 项**，对真库跑，含"启动配置印的是生效值"）；
   CI 新增「Admin CLI test」一步，整个 job **23 步**；
7. **CI 实测**（SHA `b358db2`，一轮全绿）：`make` 一次编出两个二进制、**编译 0 告警**、
   `admin_test` **10/10**（含"非默认 `TETRIS_AUTH_IP_MAX_FAILURES=77` 打印的确实是 77"），
   其余 20 步照旧通过。这一轮被 CI 抓到两个错：`ConnectionPool::MAX_POOL_SIZE` 是 private
   （6.42）、`Popen(cwd=...)` 下相对路径失效（6.43）。

**再上一轮做完了 SSH 承载：公网只开 sshd、9999 只绑回环**（`deploy/` + `TETRIS_BIND`）

- `deploy/tetris-bridge.py`：把一条 SSH 会话的 stdin/stdout 接到 `127.0.0.1:9999`，
  补 **raw 模式 / CR→CRLF / 本地回显**（`telnet` 行模式那三个坑），stdout 只留游戏字节；
- `deploy/sshd-tetris.conf`：`Match User tetris` + `ForceCommand`，关掉全部转发、
  `MaxSessions 1`、`ClientAlive*`，**末尾必须 `Match all`**（6.33）；
- `deploy/install-ssh-bridge.sh`：幂等安装（账号 / 片段 / `sshd -t` 校验 + 失败自动还原），
  顺带把 nologin 的 shell 改回 `/bin/sh`（6.32）；
- `src/main.cpp` 新增 `TETRIS_BIND`（默认 `0.0.0.0`，生产设 `127.0.0.1`）；
- **来源 IP 保真**：桥用 `SSH_CLIENT` 把真实客户端映射成专属 `127.b.c.d`，避免按 IP 限流
  退化成"全站共享一份额度"（6.35）；代价是每会话一个 Python 进程
  （CI 实测 **16.8~17.0 MB**），所以 SSH 承载的并发上限是内存、不是 CPU（6.36）；
- 测试/CI：`tests/bridge_test.py`（**8 项** + `--server` 的"穿过桥跑真协议"）、CI 加
  「SSH bridge test」（含 `ss -ltn` 断言只绑回环）与「Verify sshd snippet」（真 `sshd -t` +
  `sshd -T -C user=tetris` 核对 `ForceCommand`）；CI 实测 SHA `c143749` 全绿。

**再往前**（细节见第 5 节）：SQLite 存储层（`userdata.csv` 首次启动一次性导入）、
按 IP 兜底、改密/重置（主菜单 5）、连接池 trim、登录失败退避 + 哈希队列上限、
注册密码策略 + 未知账号假 PBKDF2、连接池预创建 2048（4.9）、`User` 棋盘 int8（4.8）、
构建 `-O2`（4.7）。

**下一步建议（按顺序，详见第 8 节）**：

1. **注册限流**（公网开放后唯一没有上限的 PBKDF2 路径）：注册哈希跑在唯一写线程上，
   一个客户端每秒能塞进 ~20 个注册，把登录的延迟拖到无界（6.30）；
2. 认证状态跨进程（可选，只有真开多 worker 才值得）；
3. 桥换成 C 版（每会话 16.8~17MB → 预期 1~2MB；SSH 承载的并发上限就是这项内存）——
   只有真上量才值得；
4. `-flto` / 连接建立路径（性能线，收益有限，服务端本来就不是瓶颈）。

> **打包/部署已完成**（`Dockerfile` + `deploy/systemd/` + 进程内日志轮转 + 每日备份 timer +
> `docs/DEPLOY.md` 的上线 checklist），不要再重复做；只有"注册限流"还是上线前的缺口。

**最有用的结论**（细节见后文）：

- **部署有三条"不写就出事"**：`LimitNOFILE=300000:300000`（systemd 默认软限制就是连接数
  天花板）、`TETRIS_BIND=127.0.0.1`（否则明文端口直上公网）、`Restart=always`（崩溃能回来）；
  上线 checklist 在 `docs/DEPLOY.md` 第 0 节，每条都带"怎么验"；
- **日志在进程内轮转**（`TETRIS_LOG_DIR` / `_MAX_BYTES` / `_MAX_FILES`，默认 10 MiB × 5 份历史 + 当前文件）：
  不用外部 logrotate，因为 spdlog 的 fd 常开 —— `create` 会让进程继续写旧 inode，
  `copytruncate` 会在文件里留 NUL 空洞（6.44）；
- **备份是系统的一部分**：`tetris-backup.timer` 每天 04:15 调 `tetris-admin backup`，
  保留 14 份、失败让 systemd 记 `failed`；恢复时必须连 `-wal` 一起去掉（6.48）；
- **认证侧三层，按代价从低到高**：IP 兜底（300 次/5 分钟）→ 用户名退避（前 5 次免延迟、
  1s 起翻倍、30s 封顶）→ 哈希队列上限 1024（队满回"服务繁忙"且不计失败）；
  两层退避状态都是**每进程**的，多 worker 时阈值实际 ×N（默认 1 worker 不受影响）；
- 登录失败按用户名退避，把单账号爆破速度从 ~65 次/秒压到 **~2 次/分钟**；按 IP 兜底挡
  "换用户名喷洒"；两层都不因成功登录清零（防重置）；
- **公网入口只剩 sshd**：玩家 `ssh tetris@host` 直接进游戏，9999 只绑 `127.0.0.1`
  （`docs/SSH_DEPLOY.md`）；桥每会话 16.8~17MB，**并发上限是内存**；
- **运维有工具了**：`tetris-admin reset-password / backup / list-users / stats`，不用停服；
  服务端启动会打印一遍生效配置，改环境变量后先看那段输出；
- **存储层是 SQLite**（`TetrisDb`）：注册一条 `INSERT`、改密一条 `UPDATE`、一局一条
  `INSERT scores`，不再整表重写；旧 `userdata.csv` 只在首次启动（空库）时导入一次；
- **`sizeof(User)` = 1992 字节**（棋盘 int8 + `peerIp` + 对齐填充），10 万连接的用户对象
  ~190MB；实测 8.5 万连接 RSS ~330MB、200 玩家 ~41MB；
- 连接池启动只预创建 2048 个，跑过大流量后空闲缓存会被 trim 回 4096 个；
- 每次按键约 **12~15 µs CPU**（游戏窗口口径）；1 万按键/秒 ≈ 0.10~0.15 单核；
  `-O2` 比 `-O0` 省 28%；10 万空闲连接只占 21%~30% 单核 —— 服务端在两种场景下都不是瓶颈；
- 30 万连接未达成（瓶颈在压测客户端）；单机 4 核无法证明多核收益；
- 输出批量把同负载 CPU 减半；128 并发登录在 CI 上 0.95~1.7s（runner 间波动大，别跨 run 比）。

## 1. 仓库与本地环境

| 项 | 位置 |
|---|---|
| 仓库 | https://github.com/22222sss/Russia-Tetris |
| 本地克隆 | `D:\dev\Russia-Tetris`（Windows 侧，文件当前是 LF） |
| Linux 侧克隆 | `/home/ly/Russia-Tetris`（WSL，ext4，编译用这份） |
| Git | `D:\dev\tools\PortableGit`（已加入用户 PATH） |
| 全局 git 配置 | `D:\dev\.gitconfig`，通过用户环境变量 `GIT_CONFIG_GLOBAL` 指向 |
| SSH key | `D:\dev\.ssh\id_ed25519`（无口令），`known_hosts` 预置 GitHub 官方指纹 |
| SSH 包装器 | `D:\dev\tools\ssh-github.exe`（源码 `ssh-github.cs`） |
| WSL 发行版 | Ubuntu-22.04，磁盘在 `D:\wsl\Ubuntu-22.04\ext4.vhdx`（约 2.3 GB） |
| 游戏客户端 | `D:\dev\tools\tetris_client.py`（**裸 TCP 时代**，只用于本机调试；走 SSH 后玩家只要 `ssh tetris@host`） |
| DSH 自带 Python | `C:\Users\13482\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`（**沙箱里唯一可用的解释器**，见 6.7） |

说明：

- 所有 Git 相关内容都在 **D 盘**，C 盘只有 WSL 的系统组件（约 2 GB）；
- 沙箱里 `sh.exe` 无法创建信号管道，所以 `core.sshCommand` 指向自编译的 `ssh-github.exe`；
- 提交身份：`22222sss <1348291944@qq.com>`；
- 两份克隆要小心混用：在 `~/` 编译前先 `git pull --ff-only`，否则跑的是旧代码；
- WSL 克隆的 `origin` 现在是 **HTTPS**（拉取可用；GitHub 偶尔会 `GnuTLS recv error (-110)`，重试或改用 SSH）；
  WSL 里**没有** GitHub SSH key，要从 WSL push 得先把 `D:\dev\.ssh\id_ed25519` 拷到 `~/.ssh/`
  （`chmod 600`），再把 URL 改回 `git@github.com:...`。

## 2. 构建与运行（WSL）

```bash
sudo apt update && sudo apt install -y build-essential libevent-dev libspdlog-dev libfmt-dev \
    libssl-dev libsqlite3-dev telnet
cd ~/Russia-Tetris && git pull --ff-only
cd src && make -j"$(nproc)"    # 产出两个二进制：tetris_game（服务端）+ tetris-admin（运维工具）
./tetris_game                  # 调试/内网：监听 0.0.0.0:9999
# 生产：TETRIS_BIND=127.0.0.1 ./tetris_game，公网入口只开 sshd（docs/SSH_DEPLOY.md）
```

所有可调项都是环境变量：

| 变量 | 默认 | 作用 |
|---|---|---|
| `TETRIS_PORT` | 9999 | 换监听端口（同机跑多实例、跨进程测试用） |
| `TETRIS_BIND` | `0.0.0.0` | 监听地址；走 SSH 承载时设 `127.0.0.1`，让 9999 只对本机的桥可见 |
| `TETRIS_WORKERS` | 1 | 多进程 worker 数；>1 时用 `SO_REUSEPORT` 抢同一端口 |
| `TETRIS_REUSEPORT` | 跟随 worker 数 | 强制开/关 `SO_REUSEPORT`（做对照实验用） |
| `TETRIS_HASH_THREADS` | 2 | 密码校验（PBKDF2）线程数；`0`/`1` 关闭线程池、退回文件线程串行（对照实验用） |
| `TETRIS_LOG_LEVEL` | warn | `debug`/`info`；非默认级别会逐条 flush |
| `TETRIS_AUTH_FREE_ATTEMPTS` | 5 | 登录失败退避：前 N 次失败不延迟 |
| `TETRIS_AUTH_BASE_DELAY_MS` | 1000 | 第 N+1 次失败后的等待（之后每次翻倍） |
| `TETRIS_AUTH_MAX_DELAY_MS` | 30000 | 单次退避上限 |
| `TETRIS_AUTH_ENTRY_TTL_MS` | 900000 | 退避记录过期时间（这么久没有新失败就清零） |
| `TETRIS_AUTH_MAX_ENTRIES` | 20000 | 最多跟踪多少个用户名（满了对新用户名 fail-open） |
| `TETRIS_AUTH_QUEUE_LIMIT` | 1024 | 哈希队列待处理上限；队满回"服务繁忙"、不计失败 |
| `TETRIS_AUTH_IP_MAX_FAILURES` | 300 | 按 IP 兜底：滑动窗口内允许的失败次数（达到即拦） |
| `TETRIS_AUTH_IP_WINDOW_MS` | 300000 | 按 IP 兜底的滑动窗口长度（5 个等长槽位） |
| `TETRIS_AUTH_IP_MAX_ENTRIES` | 4096 | 最多跟踪多少个来源 IP（满了对新 IP fail-open） |
| `TETRIS_POOL_TRIM_TARGET` | 4096 | 空闲连接池最多缓存多少个 `User`；`>= 100000` 表示不缩 |
| `TETRIS_POOL_TRIM_INTERVAL_MS` | 5000 | 多久检查一次要不要 trim（100ms 定时器里的限频） |
| `TETRIS_DB` | `tetris.db` | SQLite 库文件路径（账号 + 成绩；旧的 `userdata.csv` 只用于首次导入） |
| `TETRIS_LOG_DIR` | 空（当前目录） | 日志目录（末尾补 `/`）；生产设 `/var/log/tetris`，让日志和库分家 |
| `TETRIS_LOG_MAX_BYTES` | 10485760 | 单份日志上限；`0` = 不轮转（旧行为，log.txt 一直增长） |
| `TETRIS_LOG_MAX_FILES` | 5 | 轮转保留的**历史**份数（spdlog 的 `max_files`，当前 `log.txt` 另算）；总量 ≈ 上限 × (份数+1) |

注意：改了**头文件**之后 `make` 现在会自动重编依赖它的源文件（`-MMD -MP`）。
如果 `obj/` 是旧规则时代留下的，建议先 `make clean` 一次。

SSH 桥（`deploy/tetris-bridge.py`）有自己的一套环境变量（`TETRIS_BRIDGE_TARGET` /
`_CONNECT_TIMEOUT` / `_SOURCE_IP` / `_ECHO` / `_LOG`），见 `docs/SSH_DEPLOY.md` 第 5 节；
它们和服务端的变量互不相干，桥是独立进程。

运维侧的管理命令是另一个二进制 `tetris-admin`（`make` 会一起编出来），
读同一个 `TETRIS_DB`，见 5.管理命令行。

**默认编译选项是 `-O2 -g`**（2026-11-01 起）。`-O0` 时代的历史数据只能用于当时那一轮的对比，
**不要和 -O2 的数字混比**：

- 复现旧基线：`make clean && make OPT=-O0`；
- 查看当前级别：`make -s print-opt`（CI 的 `Verify build optimization level` 步骤会守住它，
  改回 -O0 会直接失败）；
- 要比优化级别就用 `bench.yml` 的 `Gameplay A/B` 步骤：**同一台 runner** 上交替跑两份二进制，
  不要拿两次不同 run 的数字直接相除。

客户端（Windows cmd，需要真正的交互式窗口）：

```cmd
chcp 65001
wsl -d Ubuntu-22.04 -- python3 /mnt/d/dev/tools/tetris_client.py
```

- 用 `tetris_client.py` 而不是 `telnet`：telnet 默认行模式，方向键要按回车才发，且回车不是 CRLF；
- 客户端连的是 `127.0.0.1:9999`，所以**服务端必须跑在 WSL 里**；
- 账号和成绩存在服务端工作目录的 SQLite 库 `tetris.db`（已 gitignore；WAL 会带出 `-wal`/`-shm` 两个文件）；
  旧版本的 `userdata.csv` 会在首次启动（空库）时自动导入一次，之后不再使用；
- **这只适合本机调试**。公网/生产走 SSH：玩家直接 `ssh tetris@host`，不需要额外客户端程序
  （`deploy/` + `docs/SSH_DEPLOY.md`）；那时服务端记得 `TETRIS_BIND=127.0.0.1`。

## 3. 测试与压测工具链

| 文件 | 作用 |
|---|---|
| `tests/integration_test.py` | 协议级集成测试（仅标准库，**12 项**；`--db` 指向服务端的 SQLite 库）：注册 → 登录 → 排行榜 → 选难度 → 进游戏 → 按键 → 游戏结束，外加二次登录、错误密码、**短密码注册被拒**、**未知账号与错误密码提示一致**、**失败退避与等待后放行**、**修改密码**、**加盐哈希落盘**、**旧明文登录即迁移**、快速重连 |
| `tests/password_hash_test.cpp` | 哈希层自检（CI 单独编译运行）：Python `hashlib` 已知向量交叉验证、随机盐、旧明文兼容、坏记录拒绝、**`verifyDummy` 恒 false 且耗时同量级**、**注册密码策略（UTF-8 码点计数）**、单次耗时 |
| `tests/auth_throttle_test.cpp` | 退避状态机自检（CI 单独编译运行）：注入时间，覆盖**指数增长/30s 封顶/成功清零/窗口内不延长/过期/条目上限/超长用户名** |
| `tests/ip_throttle_test.cpp` | 按 IP 兜底的状态机自检（CI 单独编译运行）：注入时间，覆盖**阈值/滑动窗口（5 槽）/拒绝不延长/跨槽累计/多 IP 隔离/条目上限/过期/格式化** |
| `tests/ip_fallback_test.py` | 按 IP 兜底的协议级用例（CI 用低阈值 `5 次/10 秒` 起服务端）：**换用户名喷洒被按 IP 拦截 → 正确密码也进不去 → 127.0.0.2 同时可登录 → 窗口滑过后恢复** |
| `tests/credential.py` | 哈希格式的 Python 实现（与 `PasswordHash.cpp` 对齐），供 mock / 集成测试 / play_test 复用 |
| `tests/db.py` | 测试侧的 SQLite 存取（schema 与 `Db.cpp` 保持一致）：`--db` 路径、等账号落库、读凭据、造账号、查成绩 |
| `tests/csv_import_test.py` | 旧 `userdata.csv` 一次性导入验证：造旧 CSV → 服务端启动自动导入 → 明文密码能登录 → 登录后库里变成加盐哈希 |
| `tests/cross_process_test.py` | 跨进程存储一致性（**5 项**）：两个服务进程共享同一个 SQLite 库（WAL + `busy_timeout`），验证 A 注册的账号能在 B 登录（哈希校验跨进程同样生效），以及 **A 改密后 B 能用新密码登录、旧密码在 B 失效**（顺带证明改密写库对另一个进程立即可见） |
| `tests/mock_server.py` | 协议 mock，**只用于在没有真服务端的环境里验证上述测试脚本本身**，不是协议规范；密码与存储行为跟真服务端对齐（写 SQLite、注册写哈希、登录校验、明文迁移、注册密码策略、改密、按 IP 兜底的滑动窗口） |
| `tests/play_test.py` | 游戏负载压测客户端：真实登录/开局/持续按键（只按左/右，不按下 → 不提前 GAME OVER）；`--seed` 直接把哈希账号写进 SQLite，避免压测时触发迁移 |
| `tests/play_bench.sh` | 按 worker 列表跑游戏负载对比，每组回传一条注解；`BENCH_BIN`/`BENCH_LABEL` 可跳过 make、直接跑预编译二进制（同机 A/B 用） |
| `tests/gameplay_stats.py` | 把 `play_test` 的 JSON + 采样 JSON 归纳成一行指标：**游戏窗口 CPU / 全程 CPU / 每按键 µs**；`play_bench.sh` 调用它 |
| `tests/play_remote.sh` | **远程**游戏负载：SSH 到独立服务端，按 1/2/4 worker 正反序各跑一轮，取回采样并汇总（解决同机抢核） |
| `tests/summarize_play.py` | 汇总远程结果：只取游戏窗口的 CPU、就绪数、按键/下行、RSS/fd/进程数，写 `summary.md` |
| `tests/login_bench.py` | 登录吞吐基准：N 个连接并发真实登录（只走到主菜单），统计就绪数、总耗时、登录/秒、延迟 p50/p95/max；`--seed` 直接造哈希账号 |
| `tests/login_bench.sh` | 造账号 → 起服务端（可给 workers 列表）→ 多轮登录基准，每轮回传 `::notice::`；CI 用它记录登录吞吐 |
| `tests/load_test.py` | 纯建连压测：`--host --port --pid --json --min-success --bind-ips`；失败按 timeout/refused/reset/fd 分类 |
| `tests/bench.sh` | 一键连接压测：调 `ulimit`/sysctl、编译、起服务、采样、分片、合并报告；`BENCH_OPT` 指定优化级别、`BENCH_BIN` 跳过编译直接跑指定二进制（同机 A/B 用） |
| `tests/sample_proc.py` | 采样服务端**进程树**的 RSS/fd/CPU（多 worker 必须按树采样） |
| `tests/merge_results.py` | 合并多分片 JSON 与采样数据，生成 `result.json` |
| `tests/bridge_test.py` | SSH 桥的自动化测试（**8 项**，不需要服务端）：字节透传 / 回车翻译 / 本地回显 / ESC 序列不回显 / stdin EOF 半关闭 / 服务端断开后桥自行退出 / 连不上时的提示与退出码 / 来源地址映射，外加**真 pty 的 raw 模式**（仅 Linux，顺带回传每会话 RSS）；给 `--server --db` 时再加一条**穿过桥跑真协议**（注册→登录→进游戏→按键） |
| `tests/admin_test.py` | `tetris-admin` 的自动化测试（**10 项**，对真库跑）：reset-password 的四种结果（哈希账号 / 旧明文即迁移 / 短密码退出码 3 / 未知账号退出码 2 / 与当前密码相同不写库）、backup 的内容一致 + `integrity_check` + 600 权限 + 拒绝覆盖 + `--force`、list-users 的"明文未迁移"标记、stats、用法退出码；`--server-bin` 时再加一条**服务端启动打印生效配置**（含一个非默认 `TETRIS_AUTH_IP_*` 必须是生效值） |
| `tests/deploy_test.py` | 部署形态检查（**10 项**，纯标准库，本地也能跑）：systemd 单元的关键项（`LimitNOFILE`/`TETRIS_BIND`/`Restart`/非 root/目录分离/加固项）、备份单元 + timer（ExecStart 脚本存在、`Unit=`、`OnCalendar`、`ReadWritePaths`）、**单元/Dockerfile 里的每个 `TETRIS_*` 都必须在 `src/` 里被 `getenv` 到**（防拼错）、Dockerfile（多阶段/非 root/HEALTHCHECK 连 127.0.0.1/钉版本/运行阶段不带 `-dev`）、`.dockerignore`（库/日志/旧 CSV 不进镜像）、compose（host 网络 + 127.0.0.1 + nofile 对齐）、`deploy/*.sh` 语法、**备份脚本行为**（失败不留半成品、按 KEEP 清理）、**恢复演练工具**（`--help` + 三类必须拒绝的输入：拿生产库当备份/备份不存在/坏文件）、`docs/DEPLOY.md` 与单元不漂移 |
| `tests/log_rotation_test.py` | 日志轮转（需要真二进制）：用 `TETRIS_LOG_MAX_BYTES=4096`/`_MAX_FILES=3`/`TETRIS_LOG_LEVEL=info` 起服务端，灌 500 条连接逼出多次轮转，断言 `log.1.txt` 出现、份数不超上限、单文件不超上限的 1.5 倍、日志落在 `TETRIS_LOG_DIR`（工作目录里不能有 `log.txt`） |

`deploy/` 是部署入口（不参与服务端编译）：`tetris-bridge.py`（桥）、`sshd-tetris.conf`
（sshd Match 片段）、`install-ssh-bridge.sh`（幂等安装 + `sshd -t` 校验 + 失败还原）、
`install-systemd.sh`（账号/目录/单元/sysctl/开机自启，幂等）、`systemd/`（`tetris-game.service`
+ 备份 service/timer）、`tetris-backup.sh`（在线备份 + 保留 N 份）、`sysctl.d/`（backlog / fd）。
安装步骤、来源 IP 映射、安全边界和排错表在 `docs/SSH_DEPLOY.md`；**上线（systemd/Docker、
日志、备份恢复、升级回滚、checklist）在 `docs/DEPLOY.md`**。仓库根还有 `Dockerfile` 与
`docker-compose.yml`。

三个 C++ 自检（`password_hash_test.cpp` / `auth_throttle_test.cpp` / `ip_throttle_test.cpp`）都能在
`src/` 下单独编译运行，文件头里有完整的 `g++ ...` 命令；CI 里各占一步，失败细节走 `::error::` 注解。
集成测试的退避用例会"连续失败直到出现提示"，不依赖环境变量——但服务端必须是默认阈值
（5 次免延迟、1s 起），把 `TETRIS_AUTH_*` 调得太大它就会超时失败。
按 IP 兜底的用例相反：**必须**用低阈值服务端（`TETRIS_AUTH_IP_MAX_FAILURES=5
TETRIS_AUTH_IP_WINDOW_MS=10000`），所以它单独起一个服务端、单独占一个 CI 步骤，
不跟默认配置的集成测试混跑（否则别人的失败会把 5 次额度吃掉）。

CI 步骤（`build.yml`，每次 push `main` 都跑，顺序如下）：

1. 编译（`-O2`；编译告警条数用 `::notice::` 回传）
→ 2. **优化级别守卫**（`make -s print-opt` 不含 -O2/-O3/-Os 直接失败）
→ 3. 测试脚本语法（`tests/*.py` py_compile + `tests/*.sh` `bash -n`）
→ 4. 密码哈希自检（`password_hash_test.cpp`，失败细节走注解）→ 5. 退避自检（`auth_throttle_test.cpp`）
→ 6. **IP 兜底自检**（`ip_throttle_test.cpp`）→ 7. 协议集成测试（12 项）
→ 8. **旧 CSV 导入**（`csv_import_test.py`：造旧 CSV → 启动自动导入 → 明文登录 → 迁移）
→ 9. **IP 兜底协议测试**（低阈值服务端 + `ip_fallback_test.py`）→ 10. 跨进程存储测试（5 项）
→ 11. 双 worker 测试（先断言两个子进程都活着，再跑完整协议测试）
→ 12. 登录吞吐基准（1/2 worker 各 2 轮，只回传 `::notice::`，不做时间断言）
→ 13. 200 连接冒烟 → 14. `bench.sh` 100 连接冒烟 → 15. **连接池 trim（3000 连接后断言池子被压回
目标值、且之后仍能接新连接）** → 16. `sizeof(User)` → 17. 头文件依赖跟踪自检
（`touch User.h` + `make -n`）→ 18. **SSH 桥测试**（`tests/bridge_test.py` 的 8 项 +
`--server` 的穿过桥真协议 + 用 `TETRIS_BIND=127.0.0.1` 起服务端后断言 `ss -ltn` 只监听回环）
→ 19. **sshd 片段语法**（装 openssh-server，`sshd -t -f` 解析 `deploy/sshd-tetris.conf`，
再用 `sshd -T -C user=tetris` 核对生效的 `ForceCommand`、并断言片段以 `Match all` 收尾）
→ 20. **管理命令测试**（`tests/admin_test.py`：对真库跑 reset-password / backup / list-users /
stats，外加服务端启动打印的生效配置）
→ 21. **部署形态测试**（`tests/deploy_test.py` 10 项静态断言 + `log_rotation_test.py` 日志轮转 +
**真的装一遍 systemd 服务**：`/proc/<pid>/limits` 的软/硬都是 300000（systemd 的
`LimitNOFILE`/`LimitNOFILESoft` 一并打出来对照——它把软硬拆成两个属性，别去比
`300000:300000` 这个字符串）、`ss -ltn` 只绑 127.0.0.1、真跑一次备份并检查 600 +
`integrity_check`、**用那份备份跑一次恢复演练（临时端口 + 完整协议）**、
`kill -9` 后等到**新的 MainPID**、`systemctl stop` 在 5 秒内完成）
→ 22. **Docker 镜像**（真 `docker build` + 真跑容器：`HEALTHCHECK` 变 healthy、`id -u` 非 0、
容器内 nofile=300000、从宿主读到 9999 的首屏字节、容器内 `tetris-admin stats` 可用）。
整个 job 现在 **24 步**（原 22 步），`timeout-minutes` 从 15 提到 30。
CI 的 apt 依赖里已加 `libssl-dev`、`libsqlite3-dev`。

`bench.yml` 只在 push 到 `bench-run` 分支或手动触发时跑，**用完记得删分支**。
push 触发时它做两件事：**同机 -O0/-O2 交错 A/B**（`Build -O0 and -O2 binaries` +
`Gameplay A/B` 两步）和 1/2/4 worker 的游戏负载对比（默认就是 -O2）；
连接建立压测仍然只在手动 `workflow_dispatch` 里跑。

```bash
bash tests/bench.sh 100000 5000 30 1 2                     # 10 万 / 双客户端
BENCH_WORKERS=2 bash tests/bench.sh 100000 5000 30 1 2     # 2 个 worker
bash tests/play_bench.sh 200 20 0.02 "1 2 4" 10009         # 游戏负载：200 玩家 × 50 次/秒
bash tests/play_remote.sh --ssh root@VPS_IP --host VPS_IP \
    --players 800 --workers "1 2 4" --rounds 2             # 远程/多核：见 docs/REMOTE_BENCH.md
```

口径说明：`服务端 CPU` 是"占满一个核 = 100%"，所以 4 核跑满约 400%。

## 4. 已跑出的数据（GitHub Actions，4 核 / 16 GB）

### 4.1 连接建立（历史数据）

| 指标 | 10 万 / 单客户端 | 10 万 / 双客户端 | 20 万 / 四客户端 |
|---|---|---|---|
| 成功建立 | 70,674（70.7%） | **100,000（100%）** | **172,891（86.5%）** |
| 失败 | 29,326（全 timeout） | 0 | 27,109（全 timeout） |
| 总耗时 | 457 s | 124 s | 248 s |
| 速率 | 155/s | 804/s | 697/s |
| 服务端 CPU | 19.9% | 18.5% | 58.4% |
| 服务端 RSS | 681.6 MB | 771.1 MB | 1376.2 MB |
| 服务端 fd 峰值 | 83,292 | 99,737 | 180,959 |

`sizeof(User)` = **6304 字节**，10 万连接约 601 MB —— 这是**棋盘改 int8 之前**的数据；
改后 1984 字节 / 189MB，实测 RSS 见 4.8（10 万目标、8.5 万建立：687.6/698.1 MB → 329.5/330.4 MB）。

### 4.2 游戏负载（2026-10-30，200 玩家真实登录 + 开局 + 持续按键，20 秒）

| 配置 | 按键速率 | 服务端下行 | 服务端 CPU | RSS |
|---|---|---|---|---|
| 1 worker | 9,575 按键/秒 | 3,861 KB/s | **51.2%** | 393 MB |
| 2 worker | 9,087 按键/秒 | 3,652 KB/s | 63.7% | 427 MB |
| 4 worker | 9,210 按键/秒 | 3,708 KB/s | 71.6% | 487 MB |

### 4.3 结论

- ~~每次按键约 0.05~0.06 ms、1 万按键/秒 ≈ 0.5~0.6 核~~ —— 那是"全程 CPU ÷ 按键数"的口径，
  **把登录 PBKDF2 摊进了按键里**，已作废。按游戏窗口口径（见 4.7），真实值是
  **-O0 约 15 µs / 按键、-O2 约 11 µs / 按键**，1 万按键/秒 ≈ **0.10~0.15 单核**；
  线性外推单 worker 要 **6~9 万按键/秒**才会打满一核（现实里压测客户端会先到上限，
  现在的脚本按 0.02s 间隔定时发，最多 ~1 万/秒）。
- **服务端在两种场景下都不是瓶颈**：10 万空闲连接只占 21%~30% 单核；1 万按键/秒只占 0.1~0.15 核。
- **多 worker 没有测出收益**：吞吐持平（甚至略降），CPU 反而更高（多进程自身的定时器/epoll/调度开销）。
- **单机 4 核无法证明多核收益**：压测端与服务端抢同一批核心。要证必须多机压测（`BENCH_REMOTE_CLIENTS`）。
- 附带观察（**已收口**）：2/4 worker 时玩家就绪数曾略降（199 → 189/191），当时怀疑是共享 `userdata.csv` 的
  `flock` 争用。密码哈希那一轮把登录改成"O(1) 凭据查找 + 哈希线程池"后，用 `bench-run` 复测
  （2026-10-31，200 玩家 / 1·2·4 worker）**三个配置都是 200/200 就绪**，该现象消失；
- ⚠️ 上表是**加盐哈希之前**测的。现在登录阶段每个玩家多一次 PBKDF2（默认 2 线程的异步哈希池，单次几十 ms），
  游戏阶段的开销不变，但复测时登录准备时间/就绪数会受影响；`play_test.py --seed` 已改为直接写哈希账号。

### 4.4 登录吞吐（2026-10-30，CI 4 核 runner，128 玩家并发登录）

用 `tests/login_bench.sh 128 "1 2" 10110 2` 实测（每配置 2 轮，取两轮区间）：

| 版本 | 1 worker | 2 worker |
|---|---|---|
| 加盐哈希刚上线（每次登录全量重建玩家表 + 单线程 PBKDF2） | 3.8s / 33.5 次/秒，p95 3.56s | 2.1s / 61 次/秒 |
| + O(1) 凭据查找 | 2.9s / 44.3 次/秒，p95 2.69s | 1.6s / 81.5 次/秒 |
| + 哈希线程池（默认 2 线程） | **1.2~1.7s / 74~108 次/秒**，p95 1.1~1.7s | 0.95~1.6s / 81~136 次/秒 |

- 单次 PBKDF2 在 CI runner 上约 **30ms**（3.8s ÷ 128）；O(1) 查找每次登录省掉约 7ms（全量重建 + 持锁），
  单这一项就有 ~25% 提升——账号越多差距越大；
- 哈希线程池再带来 **2.4×**（33.5 → 74~108 次/秒），整体 **2.2~3.2×**；
- 2 worker + 每进程 2 线程 = 4 个哈希线程，在 4 核上开始抢核，收益递减（1 worker 74~108 → 2 worker 81~136）；
  单 worker 已足够应付突发登录，需要更高吞吐再调 `TETRIS_HASH_THREADS` 或加 worker；
- ⚠️ 同一配置在不同 runner 上波动可达 ±40%（同一次提交连跑两轮则很稳：上面每格的区间就是两轮实测）。
  所以看数量级和比例，别抠绝对值。CI runner 有 SHA-NI；WSL/老 CPU 单次哈希更慢（本机 10 万次约 54ms），
  绝对数字会更低。本地复现：`bash tests/login_bench.sh 200 "1" 10099 2`。
- **2026-11-01 认证加固（失败退避 + 哈希队列上限）后复测**：128 并发 1 worker **1.46s（87.5 次/秒）**、
  2 worker **1.24s（102.9 次/秒）**，与上表一致——退避只影响失败尝试，队列上限 1024 不误伤正常突发
  （设 64 会有一半被回"繁忙"，见 6.24）。

### 4.5 30 万连接尝试（2026-11-01，未达成，瓶颈在客户端）

| 实验环境 | 目标 | 成功 | 失败 | 速率 | 总耗时 | 服务端 RSS | fd 峰值 | 服务端 CPU |
|---|---|---|---|---|---|---|---|---|
| CI 4C/16G（200k 历史） | 200,000 | 172,891（86.5%） | 27,109 timeout | 697/s | 248s | 1,376 MB | 180,959 | 58.4% |
| 本机 WSL 6C/7.4G | 200,000 | 180,247（90.1%） | 19,753 timeout | 54/s* | 3,315s* | 1,389 MB | 179,536 | 58.8% |
| CI 4C/16G（临时 `bench-300k`） | 300,000 | **182,375（60.8%）** | 117,625 timeout | 305/s | 597s | 1,273 MB | 164,252 | 52.9% |

- 两次 20 万的服务端指标几乎相同（RSS 1.38GB、fd ~18 万、0.58 核），说明服务端容量可复现；
- 30 万那次服务端只用了 **1.27GB / 0.53 核**，远未到瓶颈；失败全是客户端连接超时，
  另有 3 万条"连上但没读到欢迎数据"，是 6 个 Python 客户端进程在 4 核上互相抢 CPU 的结果；
- **结论：30 万连接仍未验证**。按 20 万线性外推，30 万约需 2.1GB RSS / 30 万 fd，服务端扛得住；
  要真正压到 30 万，需要更多客户端机器（或多机 + 多 IP），不是调服务端参数能解决的；
- *本机 20 万那行的总耗时/速率被宿主挂起污染（中途约 35 分钟零完成、p99 2,503s），
  只有容量数据（成功数 / RSS / fd）可用。

### 4.6 输出批量前后（2026-11-01，CI 4 核，200 玩家 × 20s）

同一套 `bench.yml`、同样参数，只差"把一次事件里的多次 `send` 合并成一次"：

| 版本 | workers | 按键/秒 | 下行 KB/s | 服务端 CPU | RSS |
|---|---|---|---|---|---|
| 批量前 `7ceed5b` | 1 | 7,969 | 3,084 | 114.2% | 396 MB |
| 批量前 | 2 | 8,185 | 3,284 | 155.0% | 434 MB |
| 批量前 | 4 | 8,130 | 3,279 | 162.6% | 500 MB |
| 批量后 `1f77bd4` | 1 | 8,288 | 3,335 | **51.4%** | 397 MB |
| 批量后 | 2 | 8,400 | 3,380 | **74.5%** | 435 MB |
| 批量后 | 4 | 8,391 | 3,382 | **78.1%** | 501 MB |

- 同样负载下 CPU 约**减半**（-52%~-55%），下行还略升，说明完成的工作量没有变少；
- 原因：每次按键原本要 **20~30 次 `send`**（每个方块格子 3 次：定位/颜色/文本），
  一次重力下落也一样；批量后整帧 1 次 `send`；
- ⚠️ CPU 口径包含登录 PBKDF2（每个 worker 2 个哈希线程），**不要横向比较不同 worker 数**；
  干净信号是同一 worker 数的"批量前 vs 批量后"。runner 本身有 ±40% 波动，但 2× 的差距超出噪声。
  这一轮的绝对值（51.4%）还是"全程"口径，不能直接换算成每按键 CPU——见 4.7。

### 4.7 构建优化 -O2（2026-11-01，CI 4 核，同机交错 A/B）

做法：同一次 `bench.yml` 里编两份二进制（`make OPT=-O0` / `OPT=-O2`），在**同一台 runner** 上
按 O0/O2/O0/O2 交错各跑两轮（200 玩家 × 20s，1 worker，键位与 4.6 相同）。
CPU 取**游戏窗口**口径（所有玩家都在按键的交集区间，不含登录 PBKDF2）；
每按键 µs = 窗口 CPU% × 10000 ÷ 窗口内按键速率。

| 轮次 | 优化 | 窗口 CPU | 全程 CPU | 每按键 | 按键/秒（窗口） | 下行 KB/s | RSS |
|---|---|---|---|---|---|---|---|
| 1 | -O0 | 14.8% | 32.9% | 15.3 µs | 9,668 | 3,543 | 396.9 MB |
| 2 | -O0 | 13.8% | 31.9% | 14.2 µs | 9,711 | 3,556 | 396.9 MB |
| 1 | -O2 | 10.7% | 28.9% | 11.0 µs | 9,706 | 3,558 | 396.6 MB |
| 2 | -O2 | **9.9%** | **27.5%** | **10.3 µs** | 9,653 | 3,549 | 396.5 MB |

- **窗口 CPU 14.3% → 10.3%（-28%），每按键 14.8 → 10.7 µs（-28%）**；
  两轮各自都能复现（-27.5% ~ -28.3%），不是 runner 波动；
- 按键/下行基本不变：这个压测的速率是客户端按 0.02s 间隔定时发的（上限 ~1 万/秒），
  服务端变快只会更省 CPU，不会让按键数变多；
- 全程口径只降 **13%**（32.4% → 28.2%）：登录阶段 200 次 PBKDF2 是纯 CPU 开销，
  而它跑在 OpenSSL 里、不受 `-O2` 影响，所以算进全程会把信号稀释掉——这正是本轮口径改成
  游戏窗口的原因，也是"全程 CPU 不能拿来比优化级别"的实证；
- 二进制 22.2 MB → 30.8 MB（+39%，内联与调试信息膨胀），对吞吐没有影响；
- **不要拿 4.6 的 51.4% 和本表比**：4.6 是全程口径、而且是另一台 runner；本表同一 runner 上
  全程 -O0 是 32.4%。同一次 CI 的登录基准 1 worker 只有 65.6 次/秒（4.4 的样本 74~108），
  说明这台 runner 的 PBKDF2 更慢——绝对值只能在同一 run 内比。

同一轮 `-O2` 下的 worker 扩展性（`Gameplay scaling test`，键位相同）：

| 配置 | 窗口 CPU | 全程 CPU | 每按键 | 按键/秒（窗口） | RSS | fd |
|---|---|---|---|---|---|---|
| 1 worker | 10.2% | 28.0% | 10.5 µs | 9,721 | 396.5 MB | 212 |
| 2 worker | 11.0% | 37.1% | 11.3 µs | 9,694 | 433.5 MB | 229 |
| 4 worker | 11.6% | 37.6% | 12.0 µs | 9,684 | 499.0 MB | 253 |

- 窗口口径下加 worker **只会更高**（10.2% → 11.6%，每进程一份 epoll + 100ms 定时器），
  与 4.3 的结论一致；
- 全程口径在 2/4 worker 反而更高（37%），因为每进程各有 2 个哈希线程、登录阶段把更多核拉满——
  再次说明不同 worker 数不能用全程口径比较；
- 表里的 RSS（396.5/433.5/499.0 MB）是**棋盘 int8 之前**测的；int8 后 1 worker 的 200 玩家 RSS
  降到 **190.6 MB**（4.8），连接池预创建改成 2048 后又降到 **41.1 MB**（4.9）。

CI 侧配套的两个守卫（`build.yml`）：编译告警条数用 `::notice::` 回传（本次 -O2 **0 条**，
说明没有 -O2 才暴露的未初始化/严格别名类问题）；`Verify build optimization level` 在
`make -s print-opt` 不是 -O2/-O3/-Os 时直接失败，防止以后误改回 -O0 又产出一份不可比的数据。

### 4.8 `User` 棋盘 int8（2026-11-01，CI 4 核，同机 A/B）

做法：`bench-run` 临时分支上把**改前（int）/改后（int8）两份二进制都用 -O2 编好**，
用 `BENCH_BIN` 在同一台 runner 上交替跑。连接压测按 ABBA 顺序（改前→改后→改后→改前），
游戏负载各 2 轮（改前/改后交错）。

**10 万连接（2 客户端，实际建立约 8.5 万）**

| 轮次 | 版本 | 成功建立 | 服务端 RSS 峰值 | fd 峰值 | CPU 平均 | 总耗时 | 速率 |
|---|---|---|---|---|---|---|---|
| 1 | 改前 int（6304B） | 85,499（85.5%） | 687.6 MB | 86,690 | 16.1% | 170.2 s | 502/s |
| 2 | 改后 int8（1984B） | 85,291（85.3%） | **329.5 MB** | 86,503 | 15.0% | 173.7 s | 491/s |
| 3 | 改后 int8 | 85,526（85.5%） | **330.4 MB** | 86,759 | 15.3% | 170.1 s | 503/s |
| 4 | 改前 int | 89,712（89.7%） | 698.1 MB | 88,014 | 16.5% | 215.5 s | 416/s |

**200 玩家游戏负载（20s，1 worker，-O2）**

| 轮次 | 版本 | 游戏窗口 CPU | 全程 CPU | 每按键 | 服务端 RSS |
|---|---|---|---|---|---|
| 1 | 改前 | 12.4% | 34.4% | 12.8 µs | 396.6 MB |
| 2 | 改前 | 12.5% | 34.6% | 12.9 µs | 396.6 MB |
| 1 | 改后 | 12.6% | 34.3% | 13.0 µs | **190.6 MB** |
| 2 | 改后 | 11.2% | 33.0% | 11.6 µs | **190.6 MB** |

- **RSS 直接减半**：10 万连接 687.6/698.1 → 329.5/330.4 MB（-52%）；200 玩家 396.6 → 190.6 MB（-52%）；
- 每用户对象省 4320 字节：8.5 万 × 4320 ≈ 369MB，与实测 357~369MB 的差值吻合；
  余下 ~150MB 是每个连接都要占的 fd/event/发送缓冲等非 `User` 开销，和棋盘无关；
- 100k 目标只建立 85% 是**这台 runner 慢**（502 连接/秒，4.1 的样本是 804/秒），失败全是客户端
  timeout；两个版本成功数相近（85.3%~89.7%），RSS 可以直接比——改前那轮 89.7% 的成功数更多、
  RSS 反而更高，不影响结论；
- **200 玩家场景 RSS 也减半**，因为 `ConnectionPool` 启动就预创建 50,000 个 `User`：
  改前池子本身 315MB、改后 99MB。小负载下的内存大头是池，不是活跃连接
  （当时池子还是 50k；下一轮把它改成 2048，见 4.9）；
- 游戏窗口 CPU / 每按键没有可测变化（12.4/12.5% vs 12.6/11.2%，两轮一高一低，属噪声）；
- 二进制只大 24 字节（30,837,704 → 30,837,728 B），`-O2 -g` 下没有代码膨胀；
- 按 8.5 万实测外推：**10 万连接约 350~370MB**（改前 771MB，见 4.1）。

### 4.9 连接池预创建（`INITIAL_POOL_SIZE` 50000 → 2048，2026-11-01，同机 A/B）

做法同上（`bench-run` 临时分支、`git archive` 编基线、`BENCH_BIN` 交替跑），
base = `daa64f0`（池子 50k），new = `10d95fb`（池子 2048），其余代码完全相同。

**200 玩家游戏负载（20s，1 worker，-O2）**

| 轮次 | 版本 | 游戏窗口 CPU | 全程 CPU | 每按键 | 服务端 RSS |
|---|---|---|---|---|---|
| 1 | base pool=50k | 16.0% | 44.3% | 16.4 µs | 190.4 MB |
| 2 | base pool=50k | 15.4% | 44.6% | 15.8 µs | 190.4 MB |
| 1 | new pool=2048 | 15.7% | 45.5% | 16.1 µs | **41.1 MB** |
| 2 | new pool=2048 | 15.1% | 43.8% | 15.5 µs | **41.1 MB** |

**10 万连接（2 客户端，ABBA 顺序）**

| 轮次 | 版本 | 成功建立 | 服务端 RSS 峰值 | fd 峰值 | CPU 平均 | 总耗时 | 速率 |
|---|---|---|---|---|---|---|---|
| 1 | base | 79,419（79.4%） | 308.2 MB | 80,453 | 12.8% | 240.6 s | 330/s |
| 2 | new | 82,073（82.1%） | 318.9 MB | 83,381 | 11.9% | 202.0 s | 406/s |
| 3 | new | 82,485（82.5%） | 322.1 MB | 84,482 | 11.7% | 184.0 s | 448/s |
| 4 | base | 87,517（87.5%） | 334.3 MB | 87,971 | 14.0% | 187.0 s | 468/s |

- **200 玩家 RSS 190.4 → 41.1 MB（-78%）**：省掉的 47,952 个对象 × ~3.13KB（对象 + 两个
  空 deque）≈ 150MB，与实测差值一致；剩余 41MB ≈ 基础 35MB + 池子 6MB；
- **10 万连接峰值基本不变**（308~334 vs 319~322 MB）：峰值并发下用户对象总量一样，
  只是从"启动时分配"挪到"建连时按需分配"；
- 成功率/CPU/速率没有退化：两轮中 base 的 87.5% 那轮成功数更多、CPU 也更高，
  按"每次连接 CPU"算 base 299~388µs、new 261~293µs，new 没有更差；
- 本轮 runner 比 4.8 慢（连接速率 330~468/s vs 491~503/s，游戏窗口 CPU 15~16% vs 12~13%），
  所以**绝对值不能和 4.8 的表混比**，但同 run 内的 base/new 对比有效；
- 两个二进制只差 32 字节（30,837,696 → 30,837,728 B）。

## 5. 代码里改过的关键点

### 连接层

- **修复连接静默（集成测试发现）**：连接结束时从不释放 `struct event`。内核在 `close()` 时已把 fd 从 epoll 摘除，
  而 libevent 仍认为已注册，导致新连接复用到同一 fd 时 `event_add` 不再 `epoll_ctl(ADD)`——该连接永远收不到
  可读事件。现在 `registerFdEvent` 注册前先 `event_del`+`event_free` 同 fd 旧事件（`fd < 0` 的定时器事件除外），
  并新增 `EventLoop::removeFdEvent` 在连接结束时调用；
- `User::addUser` 改用 `insert_or_assign`：原 `emplace` 在 fd 复用窗口（死连接尚未被 100ms 清理）会插入失败，
  且返回值被忽略，留下僵尸映射；
- **修复发送路径丢字节**：`output()` 原来只判断 `send` 返回 -1/0，"部分写"和 EAGAIN 都被当成成功。
  现在每个 `User` 带发送缓冲（`sendbuf` + `sendmutex` + `pendingoutput`），`flushUserOutput()` 写多少算多少，
  剩下的由 100ms 定时器补发；缓冲超过 1 MB 视为死连接并关闭；
- **异步文件回调收回事件循环线程**：三个 `*Async` 回调原来在异步文件线程上直接改 `User`（data race）。
  现在用 `EventLoop::post()` 经**自管道**投递回循环线程——没用 `evthread_use_pthreads()`（会给每个 event
  操作加全局锁）；回调里加了"状态已变化就丢弃"的守卫。

### 打包/部署（2026-11-02）

上线需要的"最后一公里"：**能一条命令装成服务、跑几个月不炸、出事有退路**。代码本身
（协议/认证/存储）一行没动，改的是部署形态 + 日志轮转 + 退出语义。

- **`deploy/install-systemd.sh`**（幂等，7 步）：装二进制与备份脚本 → 建/检查 `tetrisd`
  （系统账号、nologin、home `/var/lib/tetris`）→ 建三个目录（库 0700 / 日志 0750 / 备份 0700，
  全部属 `tetrisd`）→ 装三个单元 + `daemon-reload` → 装 `/etc/sysctl.d/99-tetris.conf` →
  `enable` + `restart` 并等到 active（最多 10 秒，不用固定 sleep）→ 自检
  （`LimitNOFILE`、`/proc/<pid>/limits`、`ss -ltn`），失败时打印 `systemctl status` 再退出；
  账号名与单元里的 `User=/Group=` **绑死**（改成环境变量会出现"脚本建了 A，单元跑 B"）；
- **`deploy/systemd/tetris-game.service`**：`LimitNOFILE=300000:300000`（软硬都写，见 6.45）、
  `TETRIS_BIND=127.0.0.1`、`TETRIS_DB=/var/lib/tetris/tetris.db`、
  `TETRIS_LOG_DIR=/var/log/tetris`、`Restart=always`+`RestartSec=2`+`StartLimitBurst=5/60s`、
  `StateDirectory`/`LogsDirectory`（systemd 负责建目录与属主）、`ProtectSystem=strict` /
  `ProtectHome` / `PrivateTmp` / `PrivateDevices` / `RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX`
  等加固项；stdout/stderr 进 journal（启动打印的"生效配置"在这里）；
- **日志轮转**（`main.cpp`）：`spdlog::basic_logger_mt` → `rotating_logger_mt`，新增
  `TETRIS_LOG_DIR`（默认当前目录，保留旧行为）、`TETRIS_LOG_MAX_BYTES`（默认 10 MiB，`0`=不轮转）、
  `TETRIS_LOG_MAX_FILES`（默认 5 份历史 + 当前文件）；轮转命名是 spdlog 的 `log.1.txt` 规则；
  打不开日志时退回 stderr 并打印原因（不让"日志目录没建"变成"服务起不来"）；
  为什么不用 logrotate 见 6.44；
- **退出语义**（`main.cpp::exit_on_signal`）：单 worker 模式装 `SIGINT`/`SIGTERM` 处理器，
  `_exit(0)` 立刻退出。原因不只是"停得慢"：容器里 `tetris_game` 是 1 号进程，而内核不会把
  "默认动作是终止"的信号投递给 1 号进程 —— 不装处理器时 `docker stop` 会等到超时才 SIGKILL；
  `_exit` 不做清理是**有意**的（信号处理器里不能安全析构线程池/日志器；SQLite 是 WAL + 事务，
  崩溃安全）；
- **备份**：`tetris-backup.timer`（04:15 + `RandomizedDelaySec=600` + `Persistent=true`）→
  `tetris-backup.service`（oneshot，`User=tetrisd`，`ReadWritePaths=/var/backups/tetris`，
  `Nice=10`）→ `deploy/tetris-backup.sh` → `tetris-admin backup`；脚本负责
  唯一文件名（同秒加后缀）、失败删半成品、空文件视为失败、按 `TETRIS_BACKUP_KEEP`
  （默认 14）删旧的；备份目录**故意不在库所在目录**（同盘写满会连库一起带走）；
- **Dockerfile**（两阶段）：`debian:bookworm` 构建（和 CI 同一组 `-dev` 依赖、
  默认 `-O2`）→ `strip` → `debian:bookworm-slim` 运行（只装运行库 + `libstdc++6`）；
  非 root（`tetris`，uid 10001）、`VOLUME /data`、`EXPOSE 9999`、`WORKDIR /data`；
  `HEALTHCHECK` 用 `/dev/tcp` 连 `127.0.0.1:9999` 并 `head -c 16` **读到首屏字节**才算健康
  （只测 listen 会漏掉"进程活着但事件循环卡死"）；容器内 `TETRIS_BIND=0.0.0.0` +
  宿主 `-p 127.0.0.1:9999:9999`（原因见 6.47）；`docker-compose.yml` 用 `network_mode: host`
  回到"绑 127.0.0.1"的形态，并带 `ulimits.nofile=300000`、两个卷、json-file 日志上限；
- **测试/CI**：`tests/deploy_test.py`（10 项，纯 stdlib，**本地可跑**；其中 `deploy/*.sh`
  语法与备份脚本行为两项在没有 POSIX shell 的机器上会跳过，可用 `TETRIS_TEST_SH` 指定）、
  `tests/log_rotation_test.py`（真二进制）；CI 末尾两步分别是"真装一遍 systemd 并跑一次备份"
  和"真 build 镜像并跑容器"，见第 3 节；
- **恢复演练工具**（`deploy/restore-drill.py`，装成 `/usr/local/bin/tetris-restore-drill`）：
  把备份复制到临时目录（`chown` 给 `tetrisd`、`chmod 600`）→ `PRAGMA integrity_check` +
  账号/成绩条数并与生产库对比（副本比生产还多就拒绝：那说明备份不是这台机器的）→
  以 `tetrisd` 身份在**另一个端口**起临时实例（独立工作目录/日志目录）→ 确认首屏菜单真的发出来 →
  找得到 `tests/integration_test.py` 时跑完整协议（12 项：注册/登录/进游戏/改密/迁移）→
  关实例、清目录。`--keep` 保留实例，用
  `sudo -u tetris TETRIS_BRIDGE_TARGET=127.0.0.1:<端口> script -q -c /usr/local/bin/tetris-bridge /dev/null`
  亲手玩副本（**桥在服务器侧**，从客户端 ssh 传环境变量 sshd 默认不收，见 6.51）；
  它只读生产库，所以可以随时演练，不必等出事；
- **`docs/DEPLOY.md`**：上线 checklist（每条都带验证命令）+ systemd/Docker 两种形态 +
  环境变量/上限 + 日志 + 备份恢复 + 升级回滚 + 巡检 + 排错表 + 安全边界；
- **CI 实测（SHA `c23d1e1`，一轮全绿）**：这一轮共 4 次 push 才绿——前三次都是只读注释
  看不出、本地也没有对应环境的东西：spdlog 头文件路径（6.50）、`LimitNOFILE` 的两个
  systemd 属性（6.45）、spdlog `max_files` 的语义、CI 用 runner 用户读 0700 备份目录。
  具体数字见第 0 节那段。

### SSH 承载（2026-11-02）

目标：**公网只开 sshd，9999 只对本机**。加密/认证/账号隔离交给 SSH，**协议一行没改**。

- **为什么是桥**：服务端本来就是"渲染终端"的程序（ANSI 全屏 + 方向键），一条 SSH 会话
  天然提供 stdin/stdout。于是让 sshd 用 `ForceCommand` 把会话交给一个小桥，桥再连
  `127.0.0.1:9999`：服务端仍是一进程多连接的事件循环，一个玩家 = 一条 TCP 连接 =
  一个 `User` 对象，和直连没有区别（连接池、限流、SQLite 全部照旧）；
- **`deploy/sshd-tetris.conf`**：`Match User tetris` + `ForceCommand /usr/local/bin/tetris-bridge`，
  关掉所有转发（`AllowTcpForwarding` / `AllowAgentForwarding` / `AllowStreamLocalForwarding` /
  `X11Forwarding` / `PermitTunnel` / `PermitUserEnvironment` / `PermitUserRC`）、
  `MaxSessions 1`（防 ControlMaster 复用出第二条游戏会话）、`ClientAliveInterval 300`
  （收掉掉线会话，否则死会话会一直占着桥进程 + `User`）；
  **末尾必须 `Match all`**（原因见 6.33）；
- **桥要替终端做三件事**（都是 6.1 那个 telnet 坑的变种）：
  1. `termios.setraw(stdin)` —— canonical 模式下方向键要按回车才发出去，游戏没法玩；
  2. CR → CRLF —— raw 终端按回车只发 `0x0D`，而 `Server::ReceiveData` 按 `"\r\n"` 分帧；
  3. 本地回显 —— 服务端不逐字符回显、raw 又关了 pty 的 echo，不补的话登录时看不见自己
     输入了什么；只回显可打印字符（含 UTF-8 多字节），**含 ESC 的整块不回显**
     （否则方向键会把整屏弄乱）；
- **stdout 只能是游戏字节**：sshd 把 stderr 也接到同一个 pty，桥的任何日志都会花屏 ——
  诊断只走 syslog 或 `TETRIS_BRIDGE_LOG` 指定的文件，永不写 stdout/stderr；
- **收尾**：stdin EOF → `shutdown(SHUT_WR)`（服务端立刻看到 EOF、释放 `User`），
  再给 2 秒把最后那屏收完；服务端先断开时桥自己退出（不会把 SSH 会话挂住）；
- **两个守护线程而不是 `select`**：Windows 的 `select` 只认 socket、不认管道/文件描述符，
  用线程同一份代码在 Windows 上也能跑测试（本地先跑通再推 CI，见 6.7）；
- **来源 IP 保真**：服务端按 accept 的源地址做 `IpThrottle` 限流，而桥接后全都来自
  127.0.0.1 → 额度会退化成"全站共享一份"。桥用 `SSH_CLIENT` 把真实客户端映射成
  **该客户端专属**的 127/8 源地址（IPv4 保留后三字节：`203.0.113.7` → `127.0.113.7`，
  同一玩家恒定、不同玩家不同；IPv6 走摘要落到 `127.255.x.y`），服务端 `ntohl(sin_addr)`
  拿到的就是一个稳定的伪匿名标识。bind 失败 / 目标不是回环时退回 127.0.0.1 并记一行日志，
  那时按"全站额度"重新估 `TETRIS_AUTH_IP_MAX_FAILURES`（见 6.35）；
- **`TETRIS_BIND`（`main.cpp`）**：监听地址从写死的 `INADDR_ANY` 改成可配，默认 `0.0.0.0`
  （旧行为不变）；生产设 `127.0.0.1` —— 否则玩家能绕过 sshd 直接 `telnet 9999`，
  明文密码照样过网，SSH 承载就白做了；
- **测试**：`tests/bridge_test.py` 8 项（含 Linux 真 pty：断言桥把 ICANON/ECHO 关掉、
  方向键不按回车就到达服务端）+ `--server` 的"穿过桥跑真协议"；CI 另加 `sshd -t` 与
  `sshd -T -C user=tetris`，把"改别人的配置"也纳入验证（写错了不是 CI 挂，是线上 sshd 起不来）；
- **代价**：每会话多一个 Python 进程（CI 实测 RSS **16.8~17.0 MB**，由 `bridge_test.py` 在
  Linux 上以 `::notice::` 回传），SSH 自己也每会话一个 sshd 进程 —— 200 个并发玩家光桥
  就是 ~3.4GB，并发天花板主要在内存，不在服务端 CPU。要抬高上限就把桥换成几十行的 C
  （预期 1~2MB/会话），见第 8 节；
- **CI 实测（SHA `c143749`，一轮全绿）**：桥的 8 项自检（含 Linux 真 pty：ICANON/ECHO
  已关、方向键不按回车就即时到达）与"穿过桥跑真协议"都通过；用 `TETRIS_BIND=127.0.0.1`
  起服务端后 `ss -ltn` 只有 `127.0.0.1:10014`；`deploy/sshd-tetris.conf` 通过真实
  `sshd -t`，`sshd -T -C user=tetris` 打印的 `ForceCommand=/usr/local/bin/tetris-bridge`；
  编译仍 0 告警。过程中被 CI 抓到的两个错：`PermitUserEnvironment` 不能写进 Match（6.38）、
  测试断言没按 mark 只看新输出（6.39）。

### 管理命令行（tetris-admin，2026-11-02）

上线必需的第二件：**没有邮箱，"忘记密码"就没有自救路径**（6.28），以前只能手工改库；
备份也一直靠记 SQLite 命令。现在是一个独立的小工具 `src/tetris_admin.cpp`
（`make` 会一起编出来），**只链 `Db` / `PasswordHash`**，不拖网络与游戏逻辑。

- **`reset-password <user>`**：交互输入两次（tty 上关回显）；`--password-stdin` 从管道读一行
  （脚本/CI 用）。走**同一套** `PasswordPolicy`（≥8 个 UTF-8 码点）与 `PasswordHash`
  （PBKDF2-SHA256，10 万次迭代），落盘就是一条 `UPDATE accounts.password` ——
  "管理员重置出来的密码"和用户在主菜单 5 改的没有任何区别，**不会出现第三套格式**；
  与当前密码相同时直接返回、不写库（省一次无谓写入，也防手滑）；
  退出码 `0` 成功 / `1` 内部错误 / `2` 用法错误或账号不存在 / `3` 新密码不满足策略；
- **`backup [目标]`**：用 SQLite **Online Backup API**（不依赖外部 `sqlite3` 命令，线上只要
  这一个二进制），WAL 下也能拿到一致快照；完成后跑 `PRAGMA integrity_check` 并统计条数，
  最后 `chmod 600`（里面是密码哈希）；目标已存在必须 `--force`。
  备份期间服务端可能还在写 —— 输出会说"快照 N / 当前 M"，**这不算失败**（快照本身一致）；
- **`list-users` / `stats`**：用户名 + 注册时间 + 成绩条数 + **是否还是旧明文**（迁移进度
  一眼可见）；stats 给账号数 / 成绩条数 / 各难度最高分；
- **服务端启动打印生效配置**（`main.cpp::printEffectiveConfig`，fork 之前只打一次）：
  二十来个环境变量，配错了只看行为很难查（`TETRIS_AUTH_IP_MAX_FAILURES` 少写一个 0，
  表现是"偶尔莫名其妙锁人"）。值全部取自各模块自己的 `configFromEnv()`/访问器；
- **不需要停服**：改密是一条 UPDATE，服务端其它进程靠 `Db::cacheStamp()`（内含 `data_version`）
  立刻能看到（跨进程测试已经证明"A 改密后 B 能用新密码登录"）；
- **`Db` 只加了两样**：`listAccounts()`（一条 SQL 拿齐四列，哈希前缀判断在 SQL 里）和
  `configuredPath()`（`TETRIS_DB` 或默认值，open 之前也能用）。schema 仍然只在 `Db.cpp`；
- **CI 实测（SHA `b358db2`，一轮全绿）**：`make` 一次编出两个二进制（**编译 0 告警**），
  `tests/admin_test.py` **10/10**（含"启动打印生效配置"：故意设非默认
  `TETRIS_AUTH_IP_MAX_FAILURES=77`，打印的确实是 77 而不是 300）；其余 20 步照旧通过。
  这一轮被 CI 抓到两个错：`ConnectionPool::MAX_POOL_SIZE` 是 private（6.42）、
  `Popen(cwd=...)` 下相对路径失效（6.43）。

### 存储层（SQLite，2026-11-02）

- **`TetrisGame/Db.{h,cpp}`**：每进程一个 `sqlite3` 连接 + 内部递归锁（查询都是微秒级，
  PBKDF2 这类慢操作仍在锁外）；`PRAGMA journal_mode=WAL` + `synchronous=NORMAL` +
  `busy_timeout=5000`，多进程可以并发读、写按行加锁；
- **表**：`accounts(username PK, password, created_at)`、`scores(id, username, difficulty,
  score, played_at)`，索引 `(username,id DESC)` / `(username,difficulty,score DESC,id)` /
  `(difficulty,score DESC,id)`；"最近 20 次"= `ORDER BY id DESC LIMIT 20`，
  排行榜 = 每个难度取 `MAX(score)` 那条（连时间戳一起）；
- **写路径**：注册 = 一条 `INSERT`（UNIQUE 天然挡重名）；改密 = 一条 `UPDATE`;
  一局结束 = 一条 `INSERT scores`。原来的"整表重写 + 临时文件 + rename + flock"全部删掉了；
- **读路径**：凭据查询走主键（登录/改密各一次），不再需要 `playerCache`；
  `PlayerInfo` 快照由 `loadPlayerData()` 从库里重建，用 `Db::cacheStamp()`
  （本进程写次数 + SQLite `data_version`）判断要不要重建——这替代了原来的 mtime/size 账本；
- **旧数据导入**：`Db::open()` 发现库空 + 目录里有 `userdata.csv` 时导入一次
  （账号/三个难度的最高分/最近成绩对），成功后写 `meta.csv_imported=1`；
  明文密码原样导入，登录时照旧迁移；
- **多 worker 首次启动**：两个进程可能同时建库/开 WAL，`busy_timeout` + 建表重试（3 次）兜住；
- **备份**：`sqlite3 tetris.db ".backup backup.db"`（或在停机时直接拷 `.db` + 两个 WAL 文件）；
  库文件里是密码哈希，权限按敏感数据处理。

### 密码存储

- `TetrisGame/PasswordHash.{h,cpp}`：PBKDF2-HMAC-SHA256（OpenSSL `PKCS5_PBKDF2_HMAC`）
  - 存储格式 `$pbkdf2-sha256$<iter>$<salt-hex>$<hash-hex>`，当前 `iter=100000`、盐 16 字节、
    摘要 32 字节；用 hex 而不是 base64 是为了不引额外编码库（这列历史上在 CSV 里，格式没变）；
  - 盐来自 `RAND_bytes`；比较用 `CRYPTO_memcmp`；
  - `verify()` 对旧版明文兼容匹配并置 `needsRehash=true`；**前缀像哈希但解析失败时直接拒绝**，
    绝不退回明文比较（否则改坏一行记录就能绕过校验）；
- 注册（`saveNewUserDataAsync`）：先在事件循环线程上**拷贝用户名/密码**（原实现直接捕获
  `shared_ptr<User>`，异步执行时连接可能已断开、对象已被池复用并清空），排进写队列后在
  **文件线程**里算哈希 → `Db::insertAccount()`（一条 INSERT，UNIQUE 挡重名）；
  ⚠️ 注册的 PBKDF2 **不在哈希线程池里、也没有任何限流**（见 6.30）；
- 登录（`Filedata::loginAsync`）：
  - `lookupCredential()` 现在是**一条主键查询**（`SELECT password FROM accounts WHERE username=?`），
    不再有"整表加载 + mtime 缓存"；账号不存在时补一次 `verifyDummy()`，保持耗时同量级；
  - 校验在**哈希线程池**里做（`TETRIS_HASH_THREADS`，默认 2；0/1 关闭退回文件线程串行），
    PBKDF2 期间不持有 SQLite 锁（`Db` 只在查询那一瞬间加锁）；
  - 校验通过且 `needsRehash` 时：投递到单写线程执行 `Db::updateCredential()`（一条 UPDATE），
    再同步 `PlayerInfo` 快照；
  - 线程池在 `initializeAsyncSystem()`（fork 之后）创建；`shutdownAsyncSystem()` 先停池、
    再停写线程、最后 `Db::close()`，保证迁移任务都落库后才关连接；
- 状态机：`STATUS_VERIFYING_PASSWORD`（枚举末尾，避免改动已有状态值）。密码送出后立即进入该状态，
  回调先校验"状态没变"再渲染；期间客户端若继续发数据，`handleClientData` 会读走丢弃
  （libevent 水平触发，不读会空转）；
- `User::initUserInfo()` 改为**只按用户名**取成绩（原来还比密码，而库里是哈希，永远比不中）；
  登录已经认证过，没必要再算一次 PBKDF2；
- `Filedata::loadPlayerData()` 用 `gLoadMutex` 把"清空 + 填充 `PlayerInfo::players`"串行化，
  内容来自 `Db::loadPlayerStats()`；只有 `Db::cacheStamp()` 变了才重建（本进程写过 / 别的进程
  提交过），所以注册/开局那次调用通常是 O(1)（快照未过期直接返回）；
- 测试：`tests/credential.py` 与 C++ 实现对齐（mock 也用它模拟哈希/迁移，集成测试用它复算库里的哈希）；
  `play_test.py --seed` / `login_bench.py --seed` 直接把哈希账号写进 SQLite，避免压测时触发迁移。

### 认证加固（2026-11-01 第一批）

- **注册密码策略**：新增 `TetrisGame/PasswordPolicy.h`（纯头文件）：
  `kMinLength = 8`（按 **UTF-8 码点**计，不是字节——否则 3 个汉字就算"8 字节"）、
  拒绝全空格；`Server::receive_password_register` 在写盘前检查，失败时不改状态、可重输，
  提示由 `UImanage::show_Password_Too_Short_Error` 渲染；
  **只在注册时检查**，登录不检查老账号，避免升级后把存量短密码用户锁在门外；
- **时序侧信道**：`PasswordHash::verifyDummy(password)` 用首次调用生成的**随机假凭据**
  （`RAND_bytes` + `std::call_once`，参数与真实凭据一致）跑一次 PBKDF2，恒返回 false。
  `Filedata::loginAsync` 在 `lookupCredential` 失败（账号不存在）时调用它，
  让"账号不存在"和"密码错误"耗时同量级；假凭据生成失败时退化为固定盐派生，保证代价不退化；
- ⚠️ 这会把"未知账号尝试"从免费变成收费：批量枚举会占满哈希线程（正常登录被拖慢），
  所以接上了第二批的失败退避 + 队列上限（见下）。

### 认证加固（2026-11-01 第二批：失败退避 + 队列上限）

- **`TetrisGame/AuthThrottle.{h,cpp}`**：纯状态机，时间由参数注入（单测不需要真时钟）。
  规则：前 `freeAttempts`（默认 5）次失败不延迟；第 k 次失败（k > free）后等待
  `min(baseDelayMs * 2^(k-free-1), maxDelayMs)`（默认 1s 起、30s 封顶）；
  `entryTtlMs`（默认 15 分钟）无新失败即过期；`onSuccess` 清零；
  `check()` 只读、**窗口内的尝试不会修改 nextAllowed**（防"靠反复触发锁死别人"）；
  超长用户名按前 64 字符归并、`maxEntries`（默认 2 万）满了对新键 fail-open；
  env：`TETRIS_AUTH_FREE_ATTEMPTS` / `_BASE_DELAY_MS` / `_MAX_DELAY_MS` / `_ENTRY_TTL_MS` / `_MAX_ENTRIES`；
- **接入点**：`Server::receive_password_load` 在提交密码前 `check()`，被节流则显示
  "登录尝试过于频繁，请 N 秒后再试："并**保持密码输入状态**（`show_Login_Throttled`）；
  登录回调里 `onFailure` / `onSuccess` 更新计数（回调在事件循环线程，状态无需加锁）；
- **哈希队列上限**：`Filedata::runHashTask` 返回 `bool`，队列长度 ≥ `hashQueueLimit()`
  （`TETRIS_AUTH_QUEUE_LIMIT`，默认 1024）时返回 false；`loginAsync` 回
  `LoginResult::Busy`，Server 显示"服务繁忙"且**不调用 onFailure**（忙 ≠ 密码错）；
- **`LoginResult`**：`Filedata::LoginResult{Ok, BadCredentials, Busy}` 替代原来的 `bool`，
  让"没校验"和"校验失败"在状态机上区分开；
- **测试**：`tests/auth_throttle_test.cpp` 覆盖 17 个边界（含"窗口内尝试不延长"、
  "条目满后 fail-open"、"超长用户名归并"）；集成测试的退避用例在**同一条连接**上做——
  多 worker 时不同连接可能落到不同进程，而退避状态是每进程的。

### 认证加固（2026-11-02 第三批：按 IP 兜底）

- **`TetrisGame/IpThrottle.{h,cpp}`**：纯状态机（时间由参数注入），key 是 IPv4（主机字节序
  `uint32_t`）。每个 IP 一条记录，记录里是 **5 个槽位**的环形数组（`bucket` 时间片编号 + 计数）；
  窗口 `windowMs` 五等分得到槽长，所以任意时刻统计的都是**最近一个完整窗口**的失败数，
  天然是滑动窗口（固定窗口会在边界放出双倍额度）；
- `check()` 只读：整窗过期的条目顺手删掉；总数 < `maxFailures` 放行；否则返回
  `retryAfterSeconds`——按"从最老的槽开始减到低于阈值"那一刻算出，也就是
  "还要等多久才有失败滑出窗口"；被拒绝的调用不改变任何计数，所以**不会延长等待**；
- `onFailure()`：只在真的校验过且失败（`LoginResult::BadCredentials`）时调用；
  条目满了（`maxEntries`）对新 IP 直接返回（fail-open），已有记录不受影响；
  进条目前按"上次清理是否超过 1 秒"节流地扫一遍过期条目；
- **接入点**：`Server::handleNewClientConnection` 把 `accept` 的 `sockaddr_storage` 转成
  `uint32_t`（`AF_INET` → `ntohl(sin_addr.s_addr)`，其它情况记 0 = 不跟踪）存到
  `User::peerIp`；`receive_password_load` 里**先判 IP、再判用户名**，
  两者都走同一个 `show_Login_Throttled`（客户端看到的是同一种提示，
  不额外泄露"是 IP 被限还是用户名被限"）；
- **成功不清零**（没有 `onSuccess`）：注册是开放的，清了就等于送攻击者一个重置开关；
- **不做单独的"惩罚时长"状态**：拦截窗口完全由计数器决定，所以不存在
  "攻击者靠反复触发把窗口越推越长"的问题；
- **测试**：`tests/ip_throttle_test.cpp`（24 项断言，CI 独立编译）+ `tests/ip_fallback_test.py`
  （协议级，低阈值服务端）；`tests/mock_server.py` 同步实现了同一套滑动窗口，
  `tests/integration_test.py` 的 `Conn` 增加 `source_ip` 参数（绑源地址，用于 127.0.0.2 对照）。

### 认证加固（2026-11-02 第四批：改密/重置）

- **入口**：登录后的主菜单新增「5. 修改密码」（`UImanage::showLoadMenu`），接
  `STATUS_CHANGE_PASSWORD_OLD` → `STATUS_CHANGE_PASSWORD_NEW`；
- **`Filedata::changePasswordAsync(username, oldPassword, newPassword, cb)`**：
  1. `lookupCredential()` 取当前凭据（一条主键查询，与登录同一路径）；
  2. `runHashTask()` 里**先** `PasswordHash::verify(oldPassword)`——错的旧密码不该白烧一次
     `hash()`；**再** `PasswordHash::hash(newPassword)`；
  3. 落盘回单写线程：`rewriteStoredCredential()`（`UPDATE accounts`）+ `updateCredentialInMemory()`；
  4. 结果 `PasswordChangeResult{Ok, BadCredentials, Busy, Error}`——比 `LoginResult` 多一个
     `Error`（生成哈希/写盘失败既不是"密码错"也不该让用户重试），用单独的枚举避免把
     `Error` 误当成登录成功；
- **限流**：提交前 `IpThrottle::check` → `AuthThrottle::check`（与登录同序）；失败回
  `BadCredentials` 时两个计数器都 `onFailure`（**这是关键**：否则拿别人的登录态就能无限
  试密码，绕开登录退避）；`Busy` 不记账；成功 `AuthThrottle::onSuccess`；
- **策略**：新密码走 `PasswordPolicy::check`（与注册一致），并拒绝"与当前密码相同"；
- **UI**：`show_Change_Password_Old` / `show_Receive_New_Password` / `show_Password_Unchanged_Error` /
  `show_Password_Changed` / `show_Change_Password_Error(message)`（错误/繁忙/被限流共用，
  一屏提示 + "请按3返回主菜单"）；
- **注意**：校验发生在**两个密码都收齐之后**（一次异步往返），所以"当前密码输入错误"是在
  用户输入完新密码之后才提示的——省一次排队，代价是提示晚一步；
- **测试**：集成测试新增「修改密码」（错误当前密码 / 短新密码 / 新密码可登录 / 旧密码失效），
  跨进程测试新增两条（A 改密 → B 用新密码登录；旧密码在 B 失效）。

### 输出批量（C3）

- `Utility/OutputBatch`（RAII）：`thread_local` 批量深度 >0 时 `output()` 只往发送缓冲追加，
  最外层析构时 `flushUserOutput()` 一次 `send` 发完；嵌套构造只累加深度；
- 包住两条高频路径：`Server::handleClientData`（一次按键/菜单事件）和
  `Game::processTimerEvent`（每个玩家一次重力下落）；
- 析构时若连接已 `OVER_QUIT` 或 `fd < 0` 就不 flush，避免对已 close 的 fd 再 send
  （fd 可能已被复用）；
- 没有给 `User` 加字段，当时 `sizeof(User)` 仍是 6304（后来棋盘改 int8 降到 1984，见下）；
  批量只影响发送时机，不改变输出顺序与内容。

### 内存（User）

- `data`/`color` 改为 `int8_t`（**有符号**：`color` 用 -1 表示空位，`memset(-1)` 对 int8_t 就是 0xFF）；
  调用点不变（仍走 `getData/setData/getColor/setColor`），赋值处显式 `static_cast<int8_t>`；
- `User.h` 末尾 `static_assert(sizeof(User) <= 2048, ...)`：结构体涨过 2KB 会直接编译失败，
  防止以后有人把棋盘改回 `int` 或加新的大字段；
- `ConnectionPool::INITIAL_POOL_SIZE = 2048`（原 50000）是**启动时预创建**的用户数
  （多 worker 按 N 均分到各进程）：每个 `User` 是 1984B 对象（现 1992B，加了 `peerIp`）
  + 两个空 deque ≈ 3.1KB，
  5 万个就是 ~150MiB；改成 2048 后池子只占 ~6MiB，池空时 `acquire` 按需创建。
  注意 `MAX_POOL_SIZE = 100000`：`release()` 会把对象缓存回池子，跑过一次 10 万连接后
  池子会涨到峰值（~300MB）——**现在由 trim 兜住**（见下）。

### 连接池收缩（trim，2026-11-02）

- **`ConnectionPool::trimIdleObjects(target)`**：把空闲缓存压回 `target` 个对象。
  先把要丢的对象一次性 `move` 出队列（持锁时间短），**在锁外 `clear()` 析构**：一次可能释放
  上万个 `User`（每个还带两个 `deque`），不该占着 `pool_mutex`；
- **`maybeTrim()`** 由 100ms 清理定时器（`EventLoop::unregister_Event_User`，正好在"把死连接
  还回池子"之后）调用，内部用 `TETRIS_POOL_TRIM_INTERVAL_MS`（默认 5000）限频，
  避免每 100ms 都和建连抢锁；
- **目标值** `TETRIS_POOL_TRIM_TARGET`（默认 4096 ≈ 13MB）；设成 `>= MAX_POOL_SIZE` 等于关闭
  （保留"只涨不缩"的旧行为）。启动时预创建 2048 个 < 4096，所以正常启动不会触发 trim；
- **为什么不会误删正在用的连接**：池里只有 `release()` 回来的空闲对象，`acquire()` 拿走的对象
  已经出池；trim 只是一个"丢掉多余空闲对象"的操作，等价于进程刚启动时的状态；
- **重新分配的成本**：池空时 `acquire()` 走 `new User(fd)`（每个 2~3µs），10 万连接那轮的
  ~85k 次重新分配摊在 200 秒里可忽略；持续游戏时池子只有几百个对象，根本不会触发 trim；
- **可观测**：`连接池 trim: A -> B (target C)`（info；默认 warn 不落盘），
  `printPerformanceStats()` 多一行"累计 trim"（`total_trimmed`）；
- **RSS 会不会回落**：只 `free` 对象时 glibc 会把空闲块留在 arena 里，RSS 不一定降——所以
  丢得比较多（≥256 个）时额外调一次 **`malloc_trim(0)`**（`#if defined(__GLIBC__)` 保护，
  非 glibc 平台只释放对象）。这一步只在 trim 时发生，开销几十 µs~几 ms，远小于一次登录；
- **CI**：`build.yml` 的「Connection pool trim test」用 `TARGET=256`/`INTERVAL=300` 起服务端，
  压 3000 连接后断言 `trim: A -> 256`、所有 trim 行的"trim 前大小"累计 ≥4200（排除
  "只是启动预创建 2048"的情况），且再压 200 连接仍 ≥90% 成功。

### 构建

- `makefile` 用 `OPT ?= -O2`（`CXXFLAGS = -Wall -g $(OPT) ...`）：`make OPT=-O0` 走旧基线，
  `print-opt` 目标把实际级别暴露给 CI 和压测脚本；链接完成时会打印级别；
- `bench.yml` 的 `Build -O0 and -O2 binaries` + `Gameplay A/B` 两步负责同机交错 A/B；
  `play_bench.sh` 支持 `BENCH_BIN`（跳过 make 跑指定二进制）/`BENCH_LABEL`（结果标签）；
  `bench.sh` 支持 `BENCH_OPT`（优化级别）和 `BENCH_BIN`，连接压测/跨提交对比都能复用；
  跨提交 A/B 的做法（4.8 用过）：临时分支上 `git -C 仓库根 archive <base-sha> | tar -x -C /tmp/base`
  编一份旧二进制，和当前提交一起用 `BENCH_BIN` 交替跑。**`git archive` 在子目录里只导出该子目录**，
  所以要 `-C` 回仓库根；
- `play_test.py` 记录每个玩家的按键窗口（`game_window`、`window_keys_per_second`），
  `tests/gameplay_stats.py` 用它算**游戏窗口 CPU** 和**每按键 µs**；
- `build.yml` 的 `Verify build optimization level` + 编译告警回传（`::notice::`/`::warning::`）。

### 其它

- 多进程 worker：`TETRIS_WORKERS=N`，父进程只 fork / 转发信号 / 回收；每个 worker 各自建
  logger（`log.<pid>.txt`）、连接池（预创建数按 N 均分）、事件循环，用 `SO_REUSEPORT` 绑同一端口。
  worker 之间不共享内存，所以 `User` 那套单线程无锁假设**不用改**；
- logger 改为**每进程创建**（异步 logger 带线程池，跨 fork 会失效，因此不能在静态初始化阶段建）；
  系统优化下沉到 `run_worker` 内（`SystemOptimizer` 会写日志，必须在 logger 就绪之后）；
- makefile 加 `-MMD -MP` + `-include $(DEPS)`：改头文件会正确触发依赖它的 `.cpp` 重编。此前只依赖 `.cpp`，
  改 `User.h` 会留下按旧布局编译的 `.o`（结构体大小不一致，运行期内存错乱）。CI 有一步用
  `touch User.h` + `make -n` 验证它没退化；
- `showRecentScores` 能识别 `Read_recent_grades` 的哨兵值 `"1"`（否则新账号会看到一个孤零零的 `1`）；
- 删除 `User::getAllUsers()`（`forEachUser` 替代后无调用点，且它按值复制整张 map，正是上次性能事故的根因）；
- `SystemOptimizer::setFileDescriptorLimit()`：目标软限制 300000，**只升不降**；
- `UImanage::clear()` 用 `\x1b[2J\x1b[H`（7 字节）；首次运行自动创建空 SQLite 库；
- README 只保留**已复测**的数据。

## 6. 已知坑

1. **telnet 不能用**：行模式 + 回车不是 CRLF → 用 `tetris_client.py`；
2. **CRLF 是个潜在坑**：工作区文件当前是 LF，但 `core.autocrlf=true` 会在下次 checkout 时转成 CRLF，
   届时 makefile 里的 `\r` 会让 make 失败 → 真遇到就 `sed -i 's/\r$//'`，或到 `~/` 编译；
3. **GitHub Actions 日志**：公开仓库下载日志需要 admin token；调试靠 `::notice::` / `::error::` 注解回传；
4. **README 图片**：GitHub 对单图有大小限制，大 GIF 压到 <10 MB（`*-demo.gif`）；
5. **沙箱限制**：pwsh 跑在低完整性，`wsl` 命令会 `E_ACCESSDENIED`；MSYS 的 `sh.exe` 无法创建信号管道；
6. **WSL 里的 `wsl` 命令**：不要在 Ubuntu 里执行，要在 Windows PowerShell 里执行；
7. **沙箱里没有本地 C++/服务端验证能力**：`wsl` 会 `E_ACCESSDENIED`，也没有 `g++`。改 C++ 只能 push 到 `main`
   让 CI 验证（`build.yml` 只在 push `main` 或 PR 时触发），结果用 GitHub API 读 conclusion + annotations；
   日志全文读不到，**所以 CI 里任何失败细节都必须用 `::error::` 注解输出**。
   **但 Python 有**：DSH 自带解释器（路径见第 1 节，`load_workspace_dependencies` 也能拿到）。
   所以改测试脚本时先用 `tests/mock_server.py` 在本地跑通再推 CI——能省掉整轮 CI 往返，实测有效；
8. **服务端日志默认只记 `warn`**：调试用 `TETRIS_LOG_LEVEL=debug`，会自动逐条 flush（异步 sink 不 flush
   的话进程被杀会丢日志尾部）。日志写到工作目录下的 `log.txt`（多 worker 是 `log.<pid>.txt`），不是 stdout；
9. **GitHub API 的 check-runs 端点数据会滞后**：要实时步骤状态就查 `/repos/:o/:r/actions/jobs/:job_id`；
   另外 job 的 "Complete job" 结束前 annotations 是空的，要等它完成再读；
10. **连续压测必须能复用 `TIME_WAIT` 端口**：同一台机器连跑多轮 10 万连接时，客户端若没设 `SO_REUSEADDR`，
    第二轮起可用端口骤减、超时暴涨（成功率 ~99% → ~81%）。**我们据此误判过一次**——以为是 `SO_REUSEPORT`
    的代价，最后用**逆序 A/B/A**才看出真正的变量是"第几次跑"。现在 `load_test.py` 已设 `SO_REUSEADDR`；
    以后做多轮对比，要么保证这个前提，要么换干净 runner。**做对照实验一定要做逆序复跑。**
11. **`bench.yml` 的 push 触发路径**只跑游戏负载对比（连接建立基准已测过），见 `Gameplay scaling test` 步骤；
    连接压测要手动 `workflow_dispatch`（填 target/concurrency/clients/workers），或临时建一个只跑
    `bench.sh 300000 ...` 的分支 workflow（用完删分支）。
12. **写文件路径别用相对路径跨 `cd`**：`play_bench.sh` 曾因此 0 秒失败——`mkdir` 在 `cd src/` 之前执行，
    之后往相对路径重定向时目录不存在，重定向本身失败，脚本立刻退出 1。
13. **PBKDF2 单次约 20~50ms**（CI runner 有 SHA-NI，本机 10 万次迭代约 54ms）。校验默认在 **2 个哈希线程**
    上并行（`TETRIS_HASH_THREADS` 可调，0/1 关闭），CI 实测单 worker **74~108 次/秒**（128 并发 1.2~1.7s，
    runner 间有波动）；2026-11-01 那台 runner 只有 **65.6 次/秒**——是机器慢，不是代码回退
    （PBKDF2 在 OpenSSL 里，-O2 不影响）。注意多 worker 时每进程各有一份池，`TETRIS_WORKERS=2` 就是
    4 个哈希线程，4 核上会开始抢核。
    `play_test.py --seed` 造 200 个账号本身也要跑 200 次 PBKDF2（本机约 11 秒）；
    如果哪天登录阶段明显拖慢压测，优先怀疑这里，而不是游戏逻辑；
14. **旧账号在首次登录前仍是明文**：这是有意设计的渐进迁移。若要做"启动即全量迁移"，注意每个账号一次
    PBKDF2，几千账号就是几分钟启动时间，必须放进后台线程并限速。
15. **`libssl-dev` 是新增构建依赖**：WSL/CI/README 都已加；老环境直接 `make` 会报 `openssl/evp.h: No such file`。
16. **远程压测必须"服务端和压测端不在同一台机器"**：同机（包括 WSL 与 Windows 宿主）永远测不出多 worker 收益。
    现成入口是 `tests/play_remote.sh` + [REMOTE_BENCH.md](REMOTE_BENCH.md)（一台按量云主机即可，几元~几十元/次）。
    另外 WSL2 默认 NAT，局域网里的其它机器**访问不到** WSL 内的 9999：要么 Windows 11 的 mirrored networking，
    要么 `netsh interface portproxy` + 防火墙放行。
17. **长压测前先禁用睡眠/合盖**：本机 WSL 跑 20 万连接时出现总耗时 55 分钟、p99 2,503s、
    中间约 35 分钟零完成——是宿主挂起/换页污染，不是服务端问题。跑长测试要插电并设成"从不睡眠"。
18. **优化级别必须和数字一起记录**：`-O0` 与 `-O2` 的绝对值不能混比，而且**"全程 CPU"也不是干净的比较口径**
    （登录 PBKDF2 会稀释甚至反转结论：本轮 -O2 全程只降 13%，游戏窗口降 28%）。
    压测结果目录的 `env.txt` 现在有"优化级别"；要比级别就用 `bench.yml` 的同机交错 A/B，
    别拿两次不同 run（更别拿不同 runner）的数字直接相除。
19. **30 万连接卡在压测端**：4 核/16G runner 上 6 个 Python 客户端只打出 182,375（60.8%），
    服务端仅 1.27GB / 53% 单核；继续加客户端进程数会互相抢核，要真压 30 万需要多台客户端机器。
20. **`sizeof(User)` 现在锁定在 1992 字节**（改 int8 之前是 6304，int8 后 1984，
    按 IP 兜底加的 `peerIp` 4 字节 + 对齐填充 +8 → 1992）：`data`/`color` 各 720 字节，
    10 万连接的用户对象约 189MB；`User.h` 里有 `static_assert(sizeof(User) <= 2048)`，
    谁把它改回 `int` 或加新字段导致超过 2KB，编译会直接失败（改之前先想清楚 10 万连接 × 每字节的代价）。
    另外每个 `User` 还有 ~1.15KB 堆开销（两个空 deque），所以"用户对象"实际约 3.1KB/连接。
    连接池启动只预创建 2048 个（`INITIAL_POOL_SIZE`，~6MiB），池空时按需创建；
    池子会涨到峰值（`MAX_POOL_SIZE = 100000`）但**空闲时会被 trim 回 4096 个**（见 5.
    连接池收缩、6.29）。`isHealthy()` 是按 `INITIAL_POOL_SIZE * 0.1` 判断的（目前没有调用点）。
21. **注册密码策略只拦注册/改密**：老账号的短密码仍能登录（否则升级即锁号），
    但登录后可以在主菜单按 **5** 改成合规密码（见 5. 认证加固第四批）——存量短密码的出路
    就是这条；没有"忘记密码/邮件重置"（没有邮箱，"重置"只能是管理员直接改 CSV）。
22. **未知账号登录现在也消耗一次 PBKDF2**（`PasswordHash::verifyDummy`）：这是有意为消除
    账号枚举时序差；配套的用户名退避挡不住"换用户名喷洒"，由 **按 IP 兜底**接住
    （见 5. 认证加固第三批、6.25）。
23. **退避状态是每进程的**：多 worker 时 N 个进程各算各的，阈值实际 ×N（默认 1 worker 不受影响）。
    集成测试的退避用例特意在**同一条连接**上做——不同连接可能被 SO_REUSEPORT 分到不同进程。
    要跨进程共享得放共享内存/文件，暂时没做。按 IP 兜底也一样是每进程的（6.25）。
24. **哈希队列上限别调太小**：`TETRIS_AUTH_QUEUE_LIMIT` 默认 1024；设成 64 时 CI 的 128 并发
    登录基准有**一半**被判 Busy（实测 66/128 就绪）。队满时服务端回"服务繁忙"且不计失败次数，
    所以它不会污染退避统计。
25. **按 IP 兜底只按"失败"计数，且是每进程的**：成功登录不清零（防刷重置），
    **注册路径不经过它**（注册在写入队列里算哈希，不占哈希线程池；要防注册刷号得另做）。
    多 worker 时每个进程各有一份计数 → 单 IP 的实际额度 ≈ 300 × worker 数，
    `maxEntries` 也是每进程 4096。阈值调低之前先想 NAT：一个出口后面几十个玩家，
    300 次/5 分钟已经很紧。来源 IP 取不到（非 IPv4）时记 0、完全不参与限流。
26. **谁都能改 `TETRIS_AUTH_IP_*` 调低阈值**，但 `tests/ip_fallback_test.py` 假设
    服务端阈值就是它传进来的 `--max-failures`：两边不一致时用例会"多试一次才拦"或
    "根本没拦"，报错信息会说明。CI 里固定 `5 次/10 秒`（默认 300/5 分钟等不起）。
27. **改密是"两个密码都收齐才校验"**：所以"当前密码错误"要等用户输入完新密码才提示
    （一次异步往返换来的；想要提前提示就得拆成两次排队，各自烧一次往返）。
    另外改密**不会踢掉已有会话**：本连接和其它连接都用的是登录时校验过的身份，
    改密只改下次登录的凭据——这个项目没有服务端会话，谈不上失效；
    改密失败会**计入该用户名的退避 + 该 IP 的失败数**（否则它就是猜密码的旁路），
    所以短时间内连续输错会看到"修改密码尝试过于频繁"。
28. **改密只支持"知道当前密码"的人**：没有邮箱/密保，忘记密码只能由管理员直接改
    库里的 `accounts.password`（写一行 `$pbkdf2-sha256$...` 或干脆写明文让它下次登录时迁移）。
29. **trim 只在"池子比目标大"时动手，且按间隔限频**：默认 4096 / 5 秒，所以跑完 10 万连接
    后那 ~300MB 缓存最多留几秒；想关掉就设 `TETRIS_POOL_TRIM_TARGET=100000`（或更大）。
    它**只丢空闲对象**，不影响在线连接；但峰值过后马上再来一波大建连会重新 `new`（每个
    2~3µs，可忽略）。多 worker 时每个进程各有一份池子、各自 trim，阈值不叠加；
    trim 的日志是 info 级别（默认 warn 不落盘），压测时想要这行就 `TETRIS_LOG_LEVEL=info`。

30. **注册路径没有任何限流**（登录有三层，注册一层都没有）：注册的 PBKDF2 跑在**唯一的异步写线程**
    上——而登录的凭据查询、成绩写入、排行榜读取也排在同一条队列里。后果不是"内存爆"，而是
    一个客户端每秒能塞进 ~20 个注册，把写线程占满后**正常登录的延迟会无界增长**；
    另外每个账号都会落库，刷号还会涨磁盘、抢用户名。**注册限流还没做**，方案见第 8 节。
31. **SQLite 的运维细节**：库文件是 `tetris.db` + `-wal`/`-shm`（三件套，`.gitignore` 已覆盖），
    备份要用 `sqlite3 tetris.db ".backup out.db"`，或停机时三件一起拷（**只拷 .db 会丢 WAL 里的新数据**）；
    库里是密码哈希，权限按敏感数据处理。旧 `userdata.csv` 只在"空库 + 文件存在"时导入一次，
    导入后不再同步（想重导得先删库）；导入发生在 `Db::open()`，所以**服务端启动目录**决定导入哪份文件。

32. **登录账号的 shell 不能是 `nologin`**：sshd 是用"用户自己的 shell -c ForceCommand"去执行
    命令的，nologin 会先把会话拒掉，玩家看到 `This account is currently not available.`，
    `ForceCommand` 根本没机会跑。`install-ssh-bridge.sh` 会自动 `chsh -s /bin/sh`
    （第一次部署时踩过：账号为安全设成 nologin，结果游戏起不来）。

33. **sshd 片段必须以 `Match all` 收尾**：`/etc/ssh/sshd_config.d/*.conf` 是被 `Include` 到
    主配置**中间**的，而 Match 块会一直生效到"文件末尾或下一个 Match"。不重置的话，主配置里
    后面的全局选项会全落进这个 Match 里 —— 看着"片段生效了"，实际行为变了，而且极难查。
    CI 的 `Verify sshd snippet` 同时校验语法和这个收尾，别删那一步。

34. **没有 pty 就没有游戏**：`ssh -T`（或 `PermitTTY no`）时 stdin 不是终端，桥不切 raw、
    也不回显，方向键会攒到回车才发 —— 现象和"telnet 不能用"一模一样。玩家必须用
    `ssh user@host`（不加 `-T`）。桥的 `--echo auto` 也是按"stdin/stdout 都是终端"判定的。

35. **桥后面按 IP 限流会退化**：所有会话都来自 127.0.0.1 时，`TETRIS_AUTH_IP_MAX_FAILURES`
    变成**全站**额度（几十个玩家正常输错就能把所有人一起锁住）。桥默认做 `127.b.c.d`
    映射来避免（见 5.SSH 承载）；映射没生效时（目标不是回环 / 平台不允许 bind 127/8 /
    `--source-ip default`）要按"全站额度"重新估阈值，或者干脆调大它。

36. **SSH 承载的并发上限是内存，不是 CPU**：每会话 = 一个 sshd 会话进程 + 一个 Python 桥进程
    （CI 实测约 **17 MB**）+ 服务端一个 `User`（3KB）。200 个并发玩家光桥就是 ~3.4GB。
    服务端自己的 CPU 仍然只占 0.1~0.15 核/万按键 —— 要提并发就换 C 版桥，别再动服务端。

37. **Windows 上 `SO_REUSEADDR` 会把"端口被残留进程占着"伪装成正常**：第二次 bind 会成功，
    但连接仍然落到老进程。现象是"本地 mock 菜单能显示、注册却没反应"（老进程的库路径已被删，
    写库失败后线程直接退出，而 mock 的 `print` 没有 flush，重定向的 stdout 里什么都看不到）。
    本地跑脚本要么换端口，要么先确认端口上是自己的进程；调 mock 时用 `python3 -u`。

38. **sshd 的 `Match` 块只允许一部分指令**：把 `PermitUserEnvironment` 写进 Match 会让
    **整个片段**被拒绝（`Directive 'PermitUserEnvironment' is not allowed within a Match block`），
    也就是 sshd 起不来 —— 不是"这条不生效"，是全部不生效。`PermitUserEnvironment` 只能全局设
    （Ubuntu/Debian 默认 `no`）。CI 的 `Verify sshd snippet` 用真的 sshd 解析片段，就是为了
    拦住这类"看着像指令、其实不能放这"的错（第一次跑就抓到了）。

39. **协议级测试的断言必须按 mark 只看新输出**：`tests/bridge_test.py` 第一版 `wait_for`
    扫全缓冲区，于是 `expect("2. 登录")` 直接命中开头的初始菜单、把 "3" 和 "2" 一次发出去 ——
    而服务端 `ReceiveData` 一次 `recv` 只保留第一个 `\r\n` 之前的内容，第二条命令被**静默丢弃**
    （现象是"菜单停了、后面全超时"）。`integration_test.py` 的 `Conn` 从一开始就带 mark，
    新写协议用例照它抄。

40. **"重置密码"不是"找回密码"**：`tetris-admin reset-password` 得管理员亲自跑，没有邮箱/密保
    这条路（6.28）。它用注册同一套策略，所以**重置一个存量 5 位老密码时会被强制变长**
    （老账号只要不重置就仍然能用短密码登录，6.21）；它也不会踢掉已登录会话
    （服务端没有会话表），新密码从下次登录生效。

41. **备份只有"在线"这一条路是安全的**：`tetris-admin backup` 走 Online Backup API，
    运行中也能拿到一致快照；**直接 cp 三件套只能停机时做**，而且只拷 `.db` 会丢掉 WAL 里
    还没 checkpoint 的数据（6.31）。备份文件里是密码哈希，工具会 `chmod 600`，
    别放进 web 目录或公共下载区。

42. **启动打印配置必须取"生效值"，不能另抄一份默认值**：`printEffectiveConfig()` 里每个数都来自
    `AuthThrottle::configFromEnv()` / `IpThrottle::configFromEnv()` / `Filedata::hash*()` /
    `ConnectionPool::trim*()` / `Db::configuredPath()`。这也是把
    `Filedata::hashThreadCount()/hashQueueLimit()` 和 `ConnectionPool::MAX_POOL_SIZE`
    从 private 挪到 public 的原因 —— 想打印就得有访问器，**别在 main.cpp 里重写一遍解析逻辑**
    （那份迟早和模块对不上，照着一个错的默认值排查比不打印更糟）。打印发生在 fork 之前
    （只打一遍），期间用临时的 bootstrap logger 接住 `configFromEnv()` 的非法值告警，打完就 drop。

43. **`subprocess.Popen(cwd=...)` 下相对路径会失效**：`tests/admin_test.py` 的"启动配置"用例
    把服务端的 cwd 设成临时工作目录（让它在自己目录里建库/写日志），于是
    `--server-bin ./tetris_game` 解析到临时目录里去了（CI 报 `No such file or directory`）。
    凡是"换了 cwd 起子进程"的地方，二进制/脚本路径要先 `os.path.abspath()`；
    本地用桩跑同样会踩（我本地传的是绝对路径，所以没暴露）。

44. **日志轮转不能交给外部 logrotate**：服务端用的是 spdlog 的文件 sink，**fd 常开**。
    `create`（rename 后新建）会让进程继续写进已经改名的旧文件，新的 `log.txt` 一直是空的；
    `copytruncate` 截断后 spdlog 的写偏移还停在原处，文件开头会留下空洞（一屏 `NUL`）。
    所以轮转放进进程里（`spdlog::rotating_file_sink`），命名是 `log.1.txt` / `log.2.txt`
    （basename + `.N` + 扩展名，别写成 `log.txt.1`）。`TETRIS_LOG_MAX_BYTES=0` 是"退回不轮转"
    的开关（老行为），**生产别设**——那正是以前把磁盘写满的配置。

45. **`LimitNOFILE` 的硬限制才是天花板**：`SystemOptimizer::setFileDescriptorLimit()` 只能把
    **软**限制抬到硬限制（要么已经够、要么 setrlimit 因为没权限失败），硬限制不够时它救不了。
    systemd 服务的默认值通常是 `1024:524288`（Ubuntu 22.04），**1024 就是能同时服务的连接数**；
    手工在 shell 里跑则只继承 shell 的 `ulimit -n`。单元里写 `LimitNOFILE=300000:300000`
    （软硬都写），验证要读 `/proc/<pid>/limits`（`awk '/Max open files/'`），不能只看
    `systemctl show`——那只是"配置值"，进程里未必生效。

46. **容器里的 1 号进程不处理"默认动作是终止"的信号**：内核不会把 `SIGTERM` 投递给 PID 1
    除非它装了处理器。所以 `docker stop` 会一直等到 `--time-limit`（默认 10s）再 SIGKILL，
    systemd 的 stop 也会拖到 `TimeoutStopSec`。两条对策都要有：`main.cpp` 里装
    `SIGTERM`/`SIGINT` 处理器（单 worker 模式，`_exit(0)`），compose 里 `init: true`
    （`docker run --init` 同理）。`_exit` 不清理是故意的：信号处理器里不能安全析构线程池，
    而 SQLite 是 WAL + 事务，硬退出不会坏库。

47. **容器里绑 127.0.0.1 就没人连得上**：`-p 127.0.0.1:9999:9999` 的 `docker-proxy`
    连的是**容器 IP**，不是容器自己的 loopback。所以镜像里必须 `TETRIS_BIND=0.0.0.0`，
    "只对公网不可见"由宿主的发布地址保证：`-p 127.0.0.1:9999:9999` ✅、`-p 9999:9999` ❌
    （后者等于把明文端口放到公网，SSH 承载白做）。用 `--network host` 时反过来：
    设 `TETRIS_BIND=127.0.0.1`，和 systemd 形态一致（compose 就是这么写的）。

48. **恢复备份必须连 `-wal`/`-shm` 一起删**：只把备份覆盖成 `tetris.db`、留着旧的
    `tetris.db-wal`，SQLite 下一次打开会把 WAL 里"更新但已不在库里"的页回放上去——
    恢复出来的数据既不是备份也不是现状。正确顺序：停服 → 挪走旧库（留证）→
    删 `.db`/`-wal`/`-shm` 三件 → 放备份 → `chown tetrisd` → 起服 → `tetris-admin stats`。
    这条也写进了 `docs/DEPLOY.md` 第 7.4 节，因为它是恢复流程里最容易漏的一步。

49. **`tetris-admin` 的库路径默认是"当前目录的 `tetris.db`"**：systemd 装法里库在
    `/var/lib/tetris/tetris.db`、服务的工作目录也是那里，但**你在别处手工跑**时它会去
    当前目录找，报"找不到数据库文件"（或者更糟：建了个空库然后说"库里还没有账号"）。
    手工用时一律带绝对路径：`sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db tetris-admin stats`。

50. **spdlog 的头文件布局随发行版版本变**：`rotating_file_sink` 和工厂函数
    `rotating_logger_mt` 都在 **`spdlog/sinks/rotating_file_sink.h`**；顶层的
    `spdlog/rotating_file_sink.h` 只是旧版本的转发头，**Ubuntu 24.04 的 libspdlog-dev(1.12)
    里没有**，写了就是 `fatal error: spdlog/rotating_file_sink.h: No such file or directory`
    —— 本轮 CI 第一次跑就抓到了这个（本地没有 g++，只能靠 CI）。改 logger 相关代码后，
    先看 Build 步骤的 `::error::` 注解（它回传前几条编译错误），别等后面的测试步骤。

51. **`TETRIS_BRIDGE_TARGET=... ssh tetris@host` 不生效**：那是**服务器侧**桥的环境变量，
    而 sshd 默认不接收客户端环境变量（`AcceptEnv` 默认是关的，我们的片段里也没有），
    所以在你笔记本上 `ssh` 时传的变量根本到不了桥。想临时把桥指向别的端口（恢复演练、
    排障）有两个办法：在服务器上起桥
    （`sudo -u tetris TETRIS_BRIDGE_TARGET=127.0.0.1:<端口> script -q -c /usr/local/bin/tetris-bridge /dev/null`，
    `script` 给它一个 pty，raw 模式/回显才正常），或临时把 `ForceCommand` 改成
    `... --target 127.0.0.1:<端口>` 再 `sshd -t && systemctl reload ssh`。
    这条是写恢复演练文档时踩出来的：`--keep` 第一版打印的正是那句不能用的命令。

## 7. 仓库现状

- `main` 干净，CI 全绿；无 tag；
- `bench-run` 分支已删除（需触发 bench 时重建并 push，用完删）；
- 远程另有 `Russia-Tetris_1.0` 分支（历史版本，未维护）；
- 许可证：MIT（`LICENSE`）；
- 遗留：
  - 构建已是 `-O2 -g`（4.7 实测每按键 CPU -28%）；`-flto`/PGO 未评估，跨 TU 内联可能还有几个点；
  - `User` 已是 int8（4.8：`sizeof` 1984；按 IP 兜底加 `peerIp` 后 **1992**）；`INITIAL_POOL_SIZE` 已降到 2048（4.9：200 玩家 RSS 41MB）；
  - 连接池**空闲时会 trim 回 4096 个**（`TETRIS_POOL_TRIM_TARGET` / `_INTERVAL_MS`，
    见 5.连接池收缩、6.29）：跑过一次 10 万连接后的 ~300MB 缓存不会再一直占着；
    trim 是"定期检查 + 只丢空闲对象"，不影响在线连接；
  - 失败退避按用户名（前 5 次免延迟、1s 起翻倍、30s 封顶，见 5.认证加固第二批）；
    **按 IP 的粗粒度兜底已完成**（5.认证加固第三批：滑动窗口 300 次/5 分钟），
    两层状态都是**每进程**的，多 worker 时阈值 ×N（见 6.23/6.25）；
  - IP 兜底不覆盖注册路径，也没有"跨进程共享计数"（要做得上共享内存/文件）；
  - 密码策略只在注册/改密时检查；**登录后可以改密**（主菜单 5），存量短密码由用户自己换；
    没有"忘记密码"（没邮箱）——现在靠管理员跑 `tetris-admin reset-password`（见 5.管理命令行、6.28）；
  - `accept` 路径的 `showInitMenu` 还没走 `OutputBatch`，菜单也没预渲染缓存；
  - 两个 100ms 定时器仍扫全表（空闲连接 CPU 的主要来源），未做"游戏中/待发/待清理"分集合；
  - 游戏负载的绝对 CPU 数字以 4.7/4.8/4.9 的同机 A/B 为准；4.1~4.6 的 CPU 都是 -O0/全程口径，
    跨 run/跨 runner 不能直接相除（见 6.18）；
  - 登录吞吐默认 2 哈希线程；更高并发调 `TETRIS_HASH_THREADS`（多 worker 时线程数按进程相乘），
    没有"按核数自动伸缩"；
  - 旧明文账号必须登录一次才会迁移（见 6.14）；旧 `userdata.csv` 只在首次启动（空库）时导入一次，
    导入后不再同步——想重跑导入得先删库（见 6.31）；
  - **SSH 承载已完成**（`deploy/` + `TETRIS_BIND` + `docs/SSH_DEPLOY.md`，见 5.SSH 承载）：
    sshd `ForceCommand` 把会话直接交给桥，公网只开 22、9999 只绑回环；
    桥现在是 Python 进程（CI 实测约 17 MB/会话，见 6.36），换 C 的收益见第 8 节；
  - **管理命令行已完成**（`tetris-admin`：`reset-password` / `backup` / `list-users` / `stats`，
    见 5.管理命令行），服务端启动也会打印一遍生效配置；
  - **打包/部署已完成**（5.打包/部署）：`Dockerfile` + `docker-compose.yml` +
    `deploy/systemd/`（`LimitNOFILE=300000`、只绑回环、加固、`Restart=always`）+
    `install-systemd.sh` + 每日备份 timer + 进程内日志轮转 + **恢复演练工具
    `tetris-restore-drill`（7.5 节：临时端口把备份恢复出来跑完整协议）** +
    `docs/DEPLOY.md` 的上线 checklist；
    CI 会**真装一遍 systemd 服务并跑一次备份**、**真 build 镜像并跑健康检查**；
    还没在真实云主机上走过一遍（本地只有 Windows + 沙箱，见 6.7）；
  - 远程压测脚本与手册已就绪（`play_remote.sh` / `REMOTE_BENCH.md`），尚未在真实云主机上跑过；
  - 连接数多机压测（`BENCH_REMOTE_CLIENTS`）修了源 IP 绑定，但 >6.4 万/台仍需加网卡 alias，未实测；
  - `output()` 的 1 MB 慢客户端上限是拍脑袋定的，未针对极端场景验证过；
  - 连接建立路径（accept/菜单）没有重测 -O2——它由内核 syscall 主导，`BENCH_OPT` 已支持，
    真要量就 `BENCH_OPT=-O0|-O2` 各跑一次 `bench.sh`。

## 8. 后续可选路线

| 方向 | 内容 | 价值 |
|---|---|---|
| **注册限流（公网前必做）** | 方案 A：按 IP（20 次/5 分钟）+ 全局（200 次/小时），提交密码前挡掉；方案 B（更重）：把注册哈希挪进哈希线程池，队满回 Busy | **高**：见 6.30，是现在唯一无上限的 PBKDF2 路径 |
| 桥换成 C 版 | 现在每会话一个 Python 进程（CI 实测约 17 MB），换几十行 C 可降到 1~2MB；SSH 承载的并发上限目前就是这项内存（见 6.36） | 中：真上量时才值得 |
| 认证状态跨进程 | 用户名退避 / IP 兜底的计数都在各 worker 内存里，阈值实际 ×N；要统一得放共享内存或落文件 | 低：只有真的开多 worker 才值得 |
| `-flto` | 跨 TU 内联（UImanage/Utility 的小函数），预期几个点；用 bench.yml 的 A/B 步骤量 | 低~中：CPU |
| 连接建立路径 | accept 时 `showInitMenu` 也批量，并把固定菜单预渲染成静态字符串 | 低~中：10 万连接场景的 CPU（当前不是瓶颈） |
| 空闲定时器 | 维护"游戏中/待发/待清理"集合，别每 100ms 扫全表 | 低：只有继续压连接数才值得 |
| 远程压测 | 有第二台机器/云主机时用 `play_remote.sh` 验证 1/2/4 worker；30 万连接已证明卡在客户端 | 中：要证多核收益 |
| 质量工程（续） | 成绩落盘后的排行榜断言（需要稳定消行）、并发多连接交互 | 低：主链路已覆盖 |

已完成（不要再当待办）：`-O2`（4.7）、`User` 棋盘 int8（4.8）、连接池预创建 2048（4.9）、
**注册密码策略 + 未知账号假 PBKDF2**、**登录失败退避 + 哈希队列上限**、
**按 IP 兜底（IpThrottle + 两套测试）**、**改密/重置（主菜单 5 + changePasswordAsync）**、
**连接池 trim（空闲缓存压回 4096，CI 有 3000 连接用例）**、
**存储层 SQLite（accounts/scores + WAL + 旧 CSV 一次性导入）**、
**SSH 承载（sshd `ForceCommand` + `deploy/` 桥 + `TETRIS_BIND`，见 5.SSH 承载）**、
**管理命令行（`tetris-admin`：reset-password / backup / list-users / stats + 启动打印生效配置，
见 5.管理命令行）**、
**打包/部署（Dockerfile + compose + systemd 单元 + 备份 timer + 日志轮转 + docs/DEPLOY.md，
见 5.打包/部署）**（本轮）、
密码加盐哈希 + 迁移、登录吞吐、输出批量。

## 9. 给新会话的起步提示

```
读 docs/HANDOVER.md 了解现状，然后我们继续做 <方向>。

方向已定：**A（终端原生，SSH 承载）+ SQLite**，目标是把现在的服务端做成能上线的产品。
功能、加固、上线入口、部署形态都已经收口：认证三层 / 改密 / 连接池 trim / SQLite 存储层 +
旧 CSV 导入 / SSH 承载（sshd ForceCommand + 桥 + TETRIS_BIND）/ 管理命令行（reset-password /
backup / list-users / stats + 启动打印生效配置）/ 打包部署（Dockerfile + compose +
systemd 单元 + 每日备份 timer + 日志轮转 + docs/DEPLOY.md 的上线 checklist）。
不要再重复做，也别再堆性能优化（建连和游戏两种场景服务端都不是瓶颈）。

上线前真正缺的（按优先级）：
1. 注册限流（公网开放前必做）：注册的 PBKDF2 跑在唯一写线程上且无上限（6.30）；
2. 真机走一遍 docs/DEPLOY.md 第 0 节的 checklist（systemd 装法 + `tetris-restore-drill`
   恢复演练），把"CI 里装过"变成"这台机器上装过"。

要点：
- 恢复演练不用等出事：`sudo tetris-restore-drill <备份>` 会把备份在临时目录/临时端口恢复出来，
  跑 integrity + 条数对比 + 完整协议（第 7.5 节）；`--keep` 后能起桥亲手玩副本；
- 数据在 tetris.db（SQLite，WAL，三件套），旧 userdata.csv 只用于首次导入；测试脚本统一用
  --db；schema 在 src/TetrisGame/Db.cpp 与 tests/db.py 两处，改结构要同时改；
- 部署入口：deploy/install-systemd.sh（服务端 + 备份 timer + sysctl，幂等）、
  deploy/install-ssh-bridge.sh（SSH 入口，幂等）、Dockerfile + docker-compose.yml；
  玩家侧只要 ssh tetris@host；细节见 docs/DEPLOY.md（上线）与 docs/SSH_DEPLOY.md（SSH 层）；
- 三条"不写就出事"：LimitNOFILE=300000:300000、TETRIS_BIND=127.0.0.1、Restart=always；
  日志在进程内轮转（TETRIS_LOG_DIR/_MAX_BYTES/_MAX_FILES），不要改成外部 logrotate（6.44）；
- 运维工具是 tetris-admin（make 一起编）：reset-password / backup / list-users / stats；
  它默认读**当前目录**的 tetris.db，手工用带 TETRIS_DB 绝对路径（6.49）；
  改环境变量后先看启动打印的"生效配置"，别猜默认值；
- 沙箱里跑不了 WSL/g++/systemd/docker/bash，C++/deploy/CI 改动只能 push main 让 CI 验证；
  失败细节靠 ::notice::/::error:: 注解。本地能跑的是 Python：tests/deploy_test.py（10 项静态
  断言，其中 2 项 shell 相关在本机会跳过）、tests/bridge_test.py（8 项）、tests/admin_test.py
  （10 项）、tests/mock_server.py + 集成 12 项 / 跨进程 5 项 / IP 兜底 —— 别浪费 CI 轮次；
- CI 现在 24 步，末尾三步是「Admin CLI test」「Deploy artifacts test (systemd)」
  「Docker image build and health check」：后两步会**真装一遍 systemd 服务**（LimitNOFILE /
  只绑回环 / 真跑备份 / kill -9 拉起 / stop 计时）和**真 build 镜像并跑健康检查**；
  改 deploy/、Dockerfile、tetris_admin.cpp 一定会碰到它们，别删；
- 压测数字的读法：CPU 看「游戏窗口 CPU / 每按键 µs」，「全程 CPU」被登录 PBKDF2 污染；
  内存看「服务端 RSS」；跨 run/跨 runner 的绝对值不要直接相除；
- 不要再做这些已完成的事：WSL 安装、README 数据清理、GIF 处理、死代码清理、质量工程、
  多核化、密码加盐哈希 + 迁移、登录吞吐、输出批量、-O2、User int8、池子 2048、
  注册密码策略、假 PBKDF2、用户名退避 + 队列上限、按 IP 兜底、改密、连接池 trim、
  SQLite 存储层、SSH 承载、管理命令行、打包/部署（Docker/systemd/日志轮转/备份 timer/恢复演练）。
```
