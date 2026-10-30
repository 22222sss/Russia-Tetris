# 交接文档（HANDOVER）

> 给"下一个会话/下一个人"看的现状说明。读完这份就不需要翻历史聊天记录。

## 0. 当前状态（截至 2026-10-30）

**本轮做完了密码加盐哈希 + 迁移**（`main` 上，CI 全绿）：

1. 新注册账号落盘即 **PBKDF2-HMAC-SHA256**（10 万次迭代 + 16 字节随机盐），
   `userdata.csv` 的 Password 列不再出现明文；
2. **旧明文账号登录即迁移**：登录成功后在 `flock` 临界区内重写该行（临时文件 +
   rename），迁移失败不影响本次登录；同进程内的缓存/`PlayerInfo` 同步更新；
3. 密码校验移到**异步线程**（单次约 50ms，放事件循环上会卡住所有连接），
   新增 `STATUS_VERIFYING_PASSWORD` 状态与回调守卫；
4. 测试补齐：`tests/password_hash_test.cpp`（Python hashlib 已知向量交叉验证，
   CI 里单独一步）+ 集成测试新增"哈希落盘""旧明文登录即迁移（含盐随机性）"两项。

**上一轮**（作为上下文）：质量工程（协议集成 + 跨进程测试）、修 4 个真实 bug
（连接静默 / 僵尸映射 / 部分写丢数据 / 异步回调跨线程改 `User`）、多核化实测收口
（单机测不出收益，默认仍 1 worker）。

**下一步建议**：远程压测（`BENCH_REMOTE_CLIENTS`，要证多核收益必须多机）；
或给登录加专用哈希线程池（见 7 节"遗留"）。

**三个最有用的结论**（细节见后文）：

- 单次游戏按键约 **0.05 ms** CPU；1 万按键/秒 ≈ 0.51 核；
- 10 万空闲连接只占 **21%~30% 单核** —— 服务端在纯建连场景从来不是瓶颈；
- 单机 4 核**无法**证明多核收益（压测端和服务端抢同一批核心），要证必须多机压测。

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
| 游戏客户端 | `D:\dev\tools\tetris_client.py` |
| DSH 自带 Python | `C:\Users\13482\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`（**沙箱里唯一可用的解释器**，见 6.7） |

说明：

- 所有 Git 相关内容都在 **D 盘**，C 盘只有 WSL 的系统组件（约 2 GB）；
- 沙箱里 `sh.exe` 无法创建信号管道，所以 `core.sshCommand` 指向自编译的 `ssh-github.exe`；
- 提交身份：`22222sss <1348291944@qq.com>`；
- 两份克隆要小心混用：在 `~/` 编译前先 `git pull --ff-only`，否则跑的是旧代码。

## 2. 构建与运行（WSL）

```bash
sudo apt update && sudo apt install -y build-essential libevent-dev libspdlog-dev libfmt-dev libssl-dev telnet
cd ~/Russia-Tetris && git pull --ff-only
cd src && make -j"$(nproc)"
./tetris_game          # 监听 0.0.0.0:9999
```

所有可调项都是环境变量：

| 变量 | 默认 | 作用 |
|---|---|---|
| `TETRIS_PORT` | 9999 | 换监听端口（同机跑多实例、跨进程测试用） |
| `TETRIS_WORKERS` | 1 | 多进程 worker 数；>1 时用 `SO_REUSEPORT` 抢同一端口 |
| `TETRIS_REUSEPORT` | 跟随 worker 数 | 强制开/关 `SO_REUSEPORT`（做对照实验用） |
| `TETRIS_HASH_THREADS` | 2 | 密码校验（PBKDF2）线程数；`0`/`1` 关闭线程池、退回文件线程串行（对照实验用） |
| `TETRIS_LOG_LEVEL` | warn | `debug`/`info`；非默认级别会逐条 flush |

注意：改了**头文件**之后 `make` 现在会自动重编依赖它的源文件（`-MMD -MP`）。
如果 `obj/` 是旧规则时代留下的，建议先 `make clean` 一次。

客户端（Windows cmd，需要真正的交互式窗口）：

```cmd
chcp 65001
wsl -d Ubuntu-22.04 -- python3 /mnt/d/dev/tools/tetris_client.py
```

- 用 `tetris_client.py` 而不是 `telnet`：telnet 默认行模式，方向键要按回车才发，且回车不是 CRLF；
- 客户端连的是 `127.0.0.1:9999`，所以**服务端必须跑在 WSL 里**；
- 账号和成绩存在服务端工作目录的 `userdata.csv`（已 gitignore）。

## 3. 测试与压测工具链

| 文件 | 作用 |
|---|---|
| `tests/integration_test.py` | 协议级集成测试（仅标准库，8 项）：注册 → 登录 → 排行榜 → 选难度 → 进游戏 → 按键 → 游戏结束，外加二次登录、错误密码、**加盐哈希落盘**、**旧明文登录即迁移**、快速重连 |
| `tests/password_hash_test.cpp` | 哈希层自检（CI 单独编译运行）：Python `hashlib` 已知向量交叉验证、随机盐、旧明文兼容、坏记录拒绝、单次耗时 |
| `tests/credential.py` | 哈希格式的 Python 实现（与 `PasswordHash.cpp` 对齐），供 mock / 集成测试 / play_test 复用 |
| `tests/cross_process_test.py` | 跨进程存储一致性（3 项）：两个服务进程共享同一份 `userdata.csv`，验证 A 注册的账号能在 B 登录（哈希校验跨进程同样生效） |
| `tests/mock_server.py` | 协议 mock，**只用于在没有真服务端的环境里验证上述测试脚本本身**，不是协议规范；密码行为与真服务端一致（注册写哈希、登录校验、明文迁移） |
| `tests/play_test.py` | 游戏负载压测客户端：真实登录/开局/持续按键（只按左/右，不按下 → 不提前 GAME OVER）；`--seed` 直接写哈希账号，避免压测时触发迁移 |
| `tests/play_bench.sh` | 按 worker 列表跑游戏负载对比，每组回传一条注解 |
| `tests/login_bench.py` | 登录吞吐基准：N 个连接并发真实登录（只走到主菜单），统计就绪数、总耗时、登录/秒、延迟 p50/p95/max；`--seed` 直接造哈希账号 |
| `tests/login_bench.sh` | 造账号 → 起服务端（可给 workers 列表）→ 多轮登录基准，每轮回传 `::notice::`；CI 用它记录登录吞吐 |
| `tests/load_test.py` | 纯建连压测：`--host --port --pid --json --min-success --bind-ips`；失败按 timeout/refused/reset/fd 分类 |
| `tests/bench.sh` | 一键连接压测：调 `ulimit`/sysctl、编译、起服务、采样、分片、合并报告 |
| `tests/sample_proc.py` | 采样服务端**进程树**的 RSS/fd/CPU（多 worker 必须按树采样） |
| `tests/merge_results.py` | 合并多分片 JSON 与采样数据，生成 `result.json` |

CI 步骤（`build.yml`，每次 push `main` 都跑，顺序如下）：

1. 编译 → 2. 密码哈希自检（`password_hash_test.cpp`，失败细节走注解）→ 3. 协议集成测试（8 项）
→ 4. 跨进程存储测试（3 项）→ 5. 双 worker 测试（先断言两个子进程都活着，再跑完整协议测试）
→ 6. 登录吞吐基准（1/2 worker 各 2 轮，只回传 `::notice::`，不做时间断言）
→ 7. 200 连接冒烟 → 8. `bench.sh` 100 连接冒烟 → 9. `sizeof(User)` → 10. 头文件依赖跟踪自检
（`touch User.h` + `make -n`）。CI 的 apt 依赖里已加 `libssl-dev`。

`bench.yml` 只在 push 到 `bench-run` 分支或手动触发时跑，**用完记得删分支**。

```bash
bash tests/bench.sh 100000 5000 30 1 2                     # 10 万 / 双客户端
BENCH_WORKERS=2 bash tests/bench.sh 100000 5000 30 1 2     # 2 个 worker
bash tests/play_bench.sh 200 20 0.02 "1 2 4" 10009         # 游戏负载：200 玩家 × 50 次/秒
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

`sizeof(User)` = **6304 字节**，10 万连接约 601 MB。

### 4.2 游戏负载（2026-10-30，200 玩家真实登录 + 开局 + 持续按键，20 秒）

| 配置 | 按键速率 | 服务端下行 | 服务端 CPU | RSS |
|---|---|---|---|---|
| 1 worker | 9,575 按键/秒 | 3,861 KB/s | **51.2%** | 393 MB |
| 2 worker | 9,087 按键/秒 | 3,652 KB/s | 63.7% | 427 MB |
| 4 worker | 9,210 按键/秒 | 3,708 KB/s | 71.6% | 487 MB |

### 4.3 结论

- **每次按键约 0.05 ms CPU**（1 万按键/秒 ≈ 0.51 核）。单 worker 撑到 ~2 万按键/秒才会打满一核。
- **服务端在两种场景下都不是瓶颈**：10 万空闲连接只占 21%~30% 单核；1 万按键/秒只占 0.51 核。
- **多 worker 没有测出收益**：吞吐持平（甚至略降），CPU 反而更高（多进程自身的定时器/epoll/调度开销）。
- **单机 4 核无法证明多核收益**：压测端与服务端抢同一批核心。要证必须多机压测（`BENCH_REMOTE_CLIENTS`）。
- 附带观察：2/4 worker 时玩家就绪数略降（199 → 189/191），怀疑与共享 `userdata.csv` 时
  登录阶段的 `flock` 争用有关，**未进一步定位**。
- ⚠️ 上表是**加盐哈希之前**测的。现在登录阶段每个玩家多一次 PBKDF2（异步线程串行，单次几十 ms），
  游戏阶段的开销不变，但复测时登录准备时间/就绪数会受影响；`play_test.py --seed` 已改为直接写哈希账号。

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

### 存储层（多进程前置）

- `userdata.csv` 的读-改-写放进 `flock` 临界区（跨进程互斥、**同线程可重入**——否则外层持锁、内层又
  `flock` 另一个 fd 会把自己锁死）；整表写入仍是"临时文件 + rename"；锁文件 `userdata.csv.lock`；
- 缓存改为按文件 `mtime`(含纳秒)/`size` 失效，否则别的进程注册的账号本进程看不到；
- `load_test.py` 的 socket 补了 `SO_REUSEADDR`（见 6.10）。

### 密码存储（本轮）

- 新增 `TetrisGame/PasswordHash.{h,cpp}`：PBKDF2-HMAC-SHA256（OpenSSL `PKCS5_PBKDF2_HMAC`）
  - 存储格式 `$pbkdf2-sha256$<iter>$<salt-hex>$<hash-hex>`，当前 `iter=100000`、盐 16 字节、
    摘要 32 字节；用 hex 而不是 base64，因为 CSV 是裸切逗号的；
  - 盐来自 `RAND_bytes`；比较用 `CRYPTO_memcmp`；
  - `verify()` 对旧版明文兼容匹配并置 `needsRehash=true`；**前缀像哈希但解析失败时直接拒绝**，
    绝不退回明文比较（否则改坏一行记录就能绕过校验）；
- 注册（`saveNewUserDataAsync`）：在**每次调用时先拷贝用户名/密码**（原实现直接捕获 `shared_ptr<User>`，
  异步线程执行时连接可能已断开、对象已被池复用并清空），再用哈希落盘、缓存也只存哈希；
- 登录（`Filedata::loginAsync`）：
  - 共享文件锁内 `loadPlayerData()` + 查库取存储值，**锁外**算 PBKDF2（不让慢哈希占着跨进程锁）；
  - 校验通过且 `needsRehash` 时：独占锁内 `rewriteStoredCredential()`（只替换该行第 2 个字段，
    临时文件带 pid、rename 覆盖、保留 `\r` 与其余字段），再同步更新缓存与 `PlayerInfo`；
  - 校验、迁移都在**异步文件线程**执行，结果用 `EventLoop::post` 回到循环线程；
- 状态机：新增 `STATUS_VERIFYING_PASSWORD`（枚举末尾，避免改动已有状态值）。密码送出后立即进入该状态，
  回调先校验"状态没变"再渲染；期间客户端若继续发数据，`handleClientData` 会读走丢弃
  （libevent 水平触发，不读会空转）；
- `User::initUserInfo()` 改为**只按用户名**取成绩（原来还比密码，而磁盘上是哈希，永远比不中）；
  登录已经认证过，没必要再算一次 PBKDF2；
- `Filedata::loadPlayerData()` 增加 `gLoadMutex`，把"清空 + 填充 `PlayerInfo::players`"串行化：
  它现在会被事件循环线程（注册/选难度）和异步线程（登录校验）同时调用，交错会重复/丢玩家；
  加锁顺序固定为"文件锁 → gLoadMutex"；
- 测试：`tests/credential.py` 与 C++ 实现对齐（mock 也用它模拟哈希/迁移，集成测试用它复算落盘哈希）；
  `play_test.py --seed` 直接写哈希账号，否则压测时每个账号都会触发一次整文件重写。

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
- `UImanage::clear()` 用 `\x1b[2J\x1b[H`（7 字节）；首次运行自动创建空 `userdata.csv`；
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
    手动 `workflow_dispatch` 才会跑连接压测；
12. **写文件路径别用相对路径跨 `cd`**：`play_bench.sh` 曾因此 0 秒失败——`mkdir` 在 `cd src/` 之前执行，
    之后往相对路径重定向时目录不存在，重定向本身失败，脚本立刻退出 1。
13. **PBKDF2 单次约几十毫秒**（本机 10 万次迭代约 54ms，CI runner 有 SHA-NI 会快不少）。校验在**单个**异步
    文件线程上串行执行，所以登录吞吐约 20 次/秒/进程；`play_test.py --seed` 造 200 个账号本身也要跑 200 次
    PBKDF2（本地约 11 秒）。如果哪天登录阶段明显拖慢压测，优先怀疑这里，而不是游戏逻辑；
14. **旧账号在首次登录前仍是明文**：这是有意设计的渐进迁移。若要做"启动即全量迁移"，注意每个账号一次
    PBKDF2，几千账号就是几分钟启动时间，必须放进后台线程并限速。
15. **`libssl-dev` 是新增构建依赖**：WSL/CI/README 都已加；老环境直接 `make` 会报 `openssl/evp.h: No such file`。

## 7. 仓库现状

- `main` 干净，CI 全绿；无 tag；
- `bench-run` 分支已删除（需触发 bench 时重建并 push，用完删）；
- 远程另有 `Russia-Tetris_1.0` 分支（历史版本，未维护）；
- 许可证：MIT（`LICENSE`）；
- 遗留：
  - 登录校验与文件写共用**一个**异步线程：高并发登录时会排队（每进程约 20 次/秒）。需要更高吞吐
    再加专用哈希线程池（PBKDF2 是纯 CPU、可并行，且 `PasswordHash` 无共享状态，改造点很集中）；
  - 旧明文账号必须登录一次才会迁移（见 6.14）；
  - 远程压测（`BENCH_REMOTE_CLIENTS`）代码在但无文档、无实测；
  - 多 worker 时登录就绪数略降（疑 `flock` 争用），未定位；
  - `output()` 的 1 MB 慢客户端上限是拍脑袋定的，未针对极端场景验证过。

## 8. 后续可选路线

| 方向 | 内容 | 价值 |
|---|---|---|
| 远程压测 | 用 `BENCH_REMOTE_CLIENTS` 跑多机，目标 30 万；同时才能验证多核收益 | 中：需多台机器 |
| 登录吞吐 | 专用哈希线程池（默认 1 线程；按需 N 线程），把单进程登录吞吐从 ~20/s 提上去 | 中：多人在线登录高峰才需要 |
| 质量工程（续） | 已覆盖主链路；仍缺：成绩落盘后的排行榜断言（需要能稳定消行）、并发多连接交互 | 中 |
| 密码策略 | 最小长度/复杂度、改密/重置、登录失败限速（防在线爆破） | 低~中：看部署场景 |

## 9. 给新会话的起步提示

```
读 docs/HANDOVER.md 了解现状，然后我们继续做 <方向>。

要点：
- 编译/运行在 WSL 的 ~/Russia-Tetris（先 git pull --ff-only），Windows 侧仓库在 D:\dev\Russia-Tetris；
- 沙箱里跑不了 WSL 和 g++，C++ 改动只能 push 到 main 让 CI 验证；失败细节靠 ::notice::/::error:: 注解回传；
- 但沙箱里有 Python：改测试脚本时先用 tests/mock_server.py 本地跑通，别浪费 CI 轮次；
- 不要再做这些已完成的事：WSL 安装、README 数据清理、GIF 处理、死代码清理、
  质量工程（集成/跨进程测试）、多核化（已实现且已有结论）、密码加盐哈希 + 迁移（已实现）。
```
