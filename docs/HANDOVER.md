# 交接文档（HANDOVER）

> 给"下一个会话/下一个人"看的现状说明。读完这份就不需要翻历史聊天记录。

## 0. 当前状态（截至 2026-11-01）

> 本次会话（2026-11-01）共四轮，按顺序：**① 构建优化 -O2**（4.7）→ **② `User` 棋盘 int8**（4.8）
> → **③ 连接池预创建 50000 → 2048**（4.9）→ **④ 认证加固**（5. 认证加固第一/二批：注册密码策略、
> 未知账号假 PBKDF2、登录失败退避、哈希队列上限）。下面只展开最后一轮，前几轮的数据见对应小节。

**本轮做完了认证加固的第二批：登录失败退避 + 哈希队列上限**（`main` 上，CI 全绿）：

1. **按用户名指数退避**（`TetrisGame/AuthThrottle.{h,cpp}`，时间可注入、可单测）：
   前 **5** 次失败免延迟；之后每失败一次延迟翻倍（**1s → 2s → … → 30s 封顶**），
   15 分钟没有新失败就过期；**成功登录立即清零**；
   窗口内的尝试只被拒绝、**不延长窗口**（否则攻击者可以靠它把别人账号永久锁死）；
   **未知用户名同样计数**（否则"被节流"又成了账号存在性的信号）；
   超长用户名按前 64 字符归并、条目上限 2 万（满了对新用户名 fail-open）；
2. **哈希队列上限**（`TETRIS_AUTH_QUEUE_LIMIT`，默认 **1024**）：原来 `hashQueue` 无界，
   登录风暴会让内存和延迟一起爆炸；队满时 `loginAsync` 回 `Busy`，服务端显示"服务繁忙"
   且**不计入失败次数**（否则"忙"会污染退避，把正常用户越推越远）。
   ⚠️ 一开始设 64，结果 CI 的 128 并发登录基准有一半被判 Busy（实测 66/128）——
   1024 约等于"2 线程 × 15 秒"的最坏排队，既容纳正常突发又挡住喷射堆积；
3. `loginAsync` 回调从 `bool` 改成 `LoginResult{Ok, BadCredentials, Busy}`；
   UI 增加 `show_Login_Throttled`（提示还要等几秒）/ `show_Server_Busy`；
4. 测试：新增 `tests/auth_throttle_test.cpp`（CI 独立编译运行，17 项断言：增长/封顶/清零/
   过期/窗口内不延长/条目上限/超长用户名）；集成测试 **10 → 11 项**（在**同一条连接**上验证
   "窗口内即使密码正确也被拒绝，等够时间后放行"，这样多 worker 也稳定）；mock 同步退避；
5. CI 实测：集成 **11/11**（单 worker + 双 worker）、退避自检全过、登录基准 **128/128**
   （1 worker 1.46s、2 worker 1.24s，没有 Busy 误伤）。

**上一轮**：认证加固第一批（注册密码最小长度 + 未知账号假 PBKDF2）；
再往前是连接池 2048（4.9）、`User` 棋盘 int8（4.8）、构建优化 -O2（4.7）。

**下一步建议（按顺序）**：

1. **按 IP 的粗粒度兜底（可选）**：现在的退避只按用户名，攻击者换用户名喷洒仍能占满哈希线程
   （队列上限只兜内存/延迟，不兜吞吐）。要做需要 accept 时记录 peer IP（`User` +4 字节，
   仍 <2048），阈值放宽松（如 300 次失败/5 分钟）以照顾 NAT；
2. **改密/重置**：配合注册密码策略，否则存量短密码没有出路；
3. **连接池只涨不缩（可选）**：`MAX_POOL_SIZE = 100000`，跑过 10 万连接后缓存 ~10 万个 `User`
   （~300MB）直到重启；要缩可以加定期 trim（或降 `MAX_POOL_SIZE`，一行，但要用压测确认复用率）；
4. 远程多机压测要第二台机器/预算，等有资源再做。

**最有用的结论**（细节见后文）：

- **登录失败按用户名退避**：前 5 次免延迟，之后 1s 起翻倍、30s 封顶；同一账号的爆破速度从
  ~65 次/秒降到 **~2 次/分钟**；窗口内尝试不延长等待（防锁号 DoS）；未知用户名同样计数；
- **哈希队列上限 1024**（`TETRIS_AUTH_QUEUE_LIMIT`），队满回"服务繁忙"、不计失败；
- 退避状态是**每进程**的：多 worker 时阈值实际 ×N（默认 1 worker 不受影响）；
- **新注册密码 ≥ 8 个字符**（UTF-8 码点）且不能全空白；**老账号不受限**；
- **账号不存在与密码错误耗时同量级**（未知账号也跑一次假 PBKDF2）；
- **`sizeof(User)` = 1984 字节**（棋盘 int8）；10 万连接用户对象 ~189MB；
  实测 8.5 万连接 RSS ~330MB、200 玩家 ~41MB（int8 + 池子瘦身之后；之前 397MB）；
- **连接池启动只预创建 2048 个 `User`**（`INITIAL_POOL_SIZE`），空闲内存 ~6MiB；
  但池子**只涨不缩**，峰值过后会保留缓存对象（见下一步建议 3）；
- 每次按键约 **12~15 µs CPU**（游戏窗口口径，不含登录）；-O2 比 -O0 省 28%；
  1 万按键/秒 ≈ **0.10~0.15 单核**；
- 10 万空闲连接只占 **21%~30% 单核** —— 服务端在纯建连场景从来不是瓶颈；
- 单机 4 核**无法**证明多核收益（压测端和服务端抢同一批核心），要证必须多机压测；
- 128 并发登录在 CI 上 **1.2~1.7s 完成**（74~108 次/秒，1 worker + 2 哈希线程），
  runner 慢的时候只有 65 次/秒——绝对值别跨 runner 比；
- **30 万连接未达成**：免费 4 核/16G runner 上最高 182,375（60.8%），失败全是客户端超时，
  服务端只用了 1.27GB / 53% 单核——瓶颈在压测端（见 4.5）；
- **输出批量把同负载 CPU 减半**：1 worker 114.2% → 51.4%（8.3k 按键/秒，下行还略升）；
- **-O2 把每按键 CPU 再降 28%**：14.8 µs → 10.7 µs（同 runner 交错 A/B，见 4.7）。

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
- 两份克隆要小心混用：在 `~/` 编译前先 `git pull --ff-only`，否则跑的是旧代码；
- WSL 克隆的 `origin` 现在是 **HTTPS**（拉取可用；GitHub 偶尔会 `GnuTLS recv error (-110)`，重试或改用 SSH）；
  WSL 里**没有** GitHub SSH key，要从 WSL push 得先把 `D:\dev\.ssh\id_ed25519` 拷到 `~/.ssh/`
  （`chmod 600`），再把 URL 改回 `git@github.com:...`。

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
| `TETRIS_AUTH_FREE_ATTEMPTS` | 5 | 登录失败退避：前 N 次失败不延迟 |
| `TETRIS_AUTH_BASE_DELAY_MS` | 1000 | 第 N+1 次失败后的等待（之后每次翻倍） |
| `TETRIS_AUTH_MAX_DELAY_MS` | 30000 | 单次退避上限 |
| `TETRIS_AUTH_ENTRY_TTL_MS` | 900000 | 退避记录过期时间（这么久没有新失败就清零） |
| `TETRIS_AUTH_MAX_ENTRIES` | 20000 | 最多跟踪多少个用户名（满了对新用户名 fail-open） |
| `TETRIS_AUTH_QUEUE_LIMIT` | 1024 | 哈希队列待处理上限；队满回"服务繁忙"、不计失败 |

注意：改了**头文件**之后 `make` 现在会自动重编依赖它的源文件（`-MMD -MP`）。
如果 `obj/` 是旧规则时代留下的，建议先 `make clean` 一次。

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
- 账号和成绩存在服务端工作目录的 `userdata.csv`（已 gitignore）。

## 3. 测试与压测工具链

| 文件 | 作用 |
|---|---|
| `tests/integration_test.py` | 协议级集成测试（仅标准库，**11 项**）：注册 → 登录 → 排行榜 → 选难度 → 进游戏 → 按键 → 游戏结束，外加二次登录、错误密码、**短密码注册被拒**、**未知账号与错误密码提示一致**、**失败退避与等待后放行**、**加盐哈希落盘**、**旧明文登录即迁移**、快速重连 |
| `tests/password_hash_test.cpp` | 哈希层自检（CI 单独编译运行）：Python `hashlib` 已知向量交叉验证、随机盐、旧明文兼容、坏记录拒绝、**`verifyDummy` 恒 false 且耗时同量级**、**注册密码策略（UTF-8 码点计数）**、单次耗时 |
| `tests/auth_throttle_test.cpp` | 退避状态机自检（CI 单独编译运行）：注入时间，覆盖**指数增长/30s 封顶/成功清零/窗口内不延长/过期/条目上限/超长用户名** |
| `tests/credential.py` | 哈希格式的 Python 实现（与 `PasswordHash.cpp` 对齐），供 mock / 集成测试 / play_test 复用 |
| `tests/cross_process_test.py` | 跨进程存储一致性（3 项）：两个服务进程共享同一份 `userdata.csv`，验证 A 注册的账号能在 B 登录（哈希校验跨进程同样生效） |
| `tests/mock_server.py` | 协议 mock，**只用于在没有真服务端的环境里验证上述测试脚本本身**，不是协议规范；密码行为与真服务端一致（注册写哈希、登录校验、明文迁移、**注册密码策略**） |
| `tests/play_test.py` | 游戏负载压测客户端：真实登录/开局/持续按键（只按左/右，不按下 → 不提前 GAME OVER）；`--seed` 直接写哈希账号，避免压测时触发迁移 |
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

两个 C++ 自检（`password_hash_test.cpp` / `auth_throttle_test.cpp`）都能在 `src/` 下单独编译运行，
文件头里有完整的 `g++ ...` 命令；CI 里各占一步，失败细节走 `::error::` 注解。
集成测试的退避用例会"连续失败直到出现提示"，不依赖环境变量——但服务端必须是默认阈值
（5 次免延迟、1s 起），把 `TETRIS_AUTH_*` 调得太大它就会超时失败。

CI 步骤（`build.yml`，每次 push `main` 都跑，顺序如下）：

1. 编译（`-O2`；编译告警条数用 `::notice::` 回传）
→ 2. **优化级别守卫**（`make -s print-opt` 不含 -O2/-O3/-Os 直接失败）
→ 3. 测试脚本语法（`tests/*.py` py_compile + `tests/*.sh` `bash -n`）
→ 4. 密码哈希自检（`password_hash_test.cpp`，失败细节走注解）→ 5. 退避自检（`auth_throttle_test.cpp`）
→ 6. 协议集成测试（11 项）→ 7. 跨进程存储测试（3 项）
→ 8. 双 worker 测试（先断言两个子进程都活着，再跑完整协议测试）
→ 9. 登录吞吐基准（1/2 worker 各 2 轮，只回传 `::notice::`，不做时间断言）
→ 10. 200 连接冒烟 → 11. `bench.sh` 100 连接冒烟 → 12. `sizeof(User)` → 13. 头文件依赖跟踪自检
（`touch User.h` + `make -n`）。CI 的 apt 依赖里已加 `libssl-dev`。

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

### 存储层（多进程前置）

- `userdata.csv` 的读-改-写放进 `flock` 临界区（跨进程互斥、**同线程可重入**——否则外层持锁、内层又
  `flock` 另一个 fd 会把自己锁死）；整表写入仍是"临时文件 + rename"；锁文件 `userdata.csv.lock`；
- 缓存改为按文件 `mtime`(含纳秒)/`size` 失效，否则别的进程注册的账号本进程看不到；
- `load_test.py` 的 socket 补了 `SO_REUSEADDR`（见 6.10）。

### 密码存储

- 新增 `TetrisGame/PasswordHash.{h,cpp}`：PBKDF2-HMAC-SHA256（OpenSSL `PKCS5_PBKDF2_HMAC`）
  - 存储格式 `$pbkdf2-sha256$<iter>$<salt-hex>$<hash-hex>`，当前 `iter=100000`、盐 16 字节、
    摘要 32 字节；用 hex 而不是 base64，因为 CSV 是裸切逗号的；
  - 盐来自 `RAND_bytes`；比较用 `CRYPTO_memcmp`；
  - `verify()` 对旧版明文兼容匹配并置 `needsRehash=true`；**前缀像哈希但解析失败时直接拒绝**，
    绝不退回明文比较（否则改坏一行记录就能绕过校验）；
- 注册（`saveNewUserDataAsync`）：在**每次调用时先拷贝用户名/密码**（原实现直接捕获 `shared_ptr<User>`，
  异步线程执行时连接可能已断开、对象已被池复用并清空），再用哈希落盘、缓存也只存哈希；
- 登录（`Filedata::loginAsync`）：
  - `lookupCredential()` 只查一个用户：缓存有效（mtime/size 没变）时是 **O(1) map 查找**，
    不再为每次登录重建整个 `PlayerInfo`；缓存失效（首次登录 / 别的进程写过文件）才完整加载一次；
  - 拿到存储值后**释放文件锁**再算 PBKDF2（不让慢哈希占着跨进程锁）；
  - 校验通过且 `needsRehash` 时：独占锁内 `rewriteStoredCredential()`（只替换该行第 2 个字段，
    临时文件带 pid、rename 覆盖、保留 `\r` 与其余字段），再同步更新缓存与 `PlayerInfo`；
  - 校验在**哈希线程池**里做（`TETRIS_HASH_THREADS`，默认 2；0/1 关闭退回文件线程串行），
    迁移的落盘任务投递回**单写线程**（写队列），结果用 `EventLoop::post` 回到循环线程；
  - 线程池在 `initializeAsyncSystem()`（fork 之后）创建，`shutdownAsyncSystem()` 先停池再停文件线程，
    保证迁移任务都入队后才结束文件线程；
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
  （多 worker 按 N 均分到各进程）：每个 `User` 是 1984B 对象 + 两个空 deque ≈ 3.1KB，
  5 万个就是 ~150MiB；改成 2048 后池子只占 ~6MiB，池空时 `acquire` 按需创建。
  注意 `MAX_POOL_SIZE = 100000`：池子**只涨不缩**，跑过一次 10 万连接后会缓存到峰值（~300MB），
  直到重启（见 7.遗留）。

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
20. **`sizeof(User)` 现在锁定在 1984 字节**（改 int8 之前是 6304）：`data`/`color` 各 720 字节，
    10 万连接的用户对象约 189MB；`User.h` 里有 `static_assert(sizeof(User) <= 2048)`，
    谁把它改回 `int` 或加新字段导致超过 2KB，编译会直接失败（改之前先想清楚 10 万连接 × 每字节的代价）。
    另外每个 `User` 还有 ~1.15KB 堆开销（两个空 deque），所以"用户对象"实际约 3.1KB/连接。
    连接池启动只预创建 2048 个（`INITIAL_POOL_SIZE`，~6MiB），池空时按需创建；
    但池子**只涨不缩**（`MAX_POOL_SIZE = 100000`），跑过 10 万连接后会缓存到峰值。`isHealthy()`
    是按 `INITIAL_POOL_SIZE * 0.1` 判断的（目前没有调用点）。
21. **注册密码策略只拦注册**：老账号的短密码仍能登录（否则升级即锁号）；目前没有"改密码/重置"
    入口，所以存量短密码只能等用户自己换——要做得先加改密流程（也要走同一套策略）。
22. **未知账号登录现在也消耗一次 PBKDF2**（`PasswordHash::verifyDummy`）：这是有意为消除
    账号枚举时序差；配套的失败退避只按用户名计数，换用户名喷洒仍能占满哈希线程，
    队列上限（1024）只兜内存/延迟。**按 IP 的兜底还没做**。
23. **退避状态是每进程的**：多 worker 时 N 个进程各算各的，阈值实际 ×N（默认 1 worker 不受影响）。
    集成测试的退避用例特意在**同一条连接**上做——不同连接可能被 SO_REUSEPORT 分到不同进程。
    要跨进程共享得放共享内存/文件，暂时没做。
24. **哈希队列上限别调太小**：`TETRIS_AUTH_QUEUE_LIMIT` 默认 1024；设成 64 时 CI 的 128 并发
    登录基准有**一半**被判 Busy（实测 66/128 就绪）。队满时服务端回"服务繁忙"且不计失败次数，
    所以它不会污染退避统计。

## 7. 仓库现状

- `main` 干净，CI 全绿；无 tag；
- `bench-run` 分支已删除（需触发 bench 时重建并 push，用完删）；
- 远程另有 `Russia-Tetris_1.0` 分支（历史版本，未维护）；
- 许可证：MIT（`LICENSE`）；
- 遗留：
  - 构建已是 `-O2 -g`（4.7 实测每按键 CPU -28%）；`-flto`/PGO 未评估，跨 TU 内联可能还有几个点；
  - `User` 已是 int8（4.8：`sizeof` 1984）；`INITIAL_POOL_SIZE` 已降到 2048（4.9：200 玩家 RSS 41MB）；
  - 连接池**只涨不缩**：`MAX_POOL_SIZE = 100000`，跑过一次 10 万连接后会缓存 ~10 万个 `User`
    （~300MB）直到重启；要缩得加定期 trim 或降 `MAX_POOL_SIZE`，改前先用连接压测确认复用率；
  - 失败退避只按用户名（前 5 次免延迟、1s 起翻倍、30s 封顶，见 5.认证加固第二批）；
    **按 IP 的粗粒度兜底没做**，换用户名喷洒仍能占满哈希线程（队列上限只兜内存/延迟）；
    退避状态是**每进程**的，多 worker 时阈值 ×N（见 6.23）；
  - 密码策略只拦注册、且没有改密码/重置入口，存量短密码暂时只能等用户自己换；
  - `accept` 路径的 `showInitMenu` 还没走 `OutputBatch`，菜单也没预渲染缓存；
  - 两个 100ms 定时器仍扫全表（空闲连接 CPU 的主要来源），未做"游戏中/待发/待清理"分集合；
  - 游戏负载的绝对 CPU 数字以 4.7/4.8/4.9 的同机 A/B 为准；4.1~4.6 的 CPU 都是 -O0/全程口径，
    跨 run/跨 runner 不能直接相除（见 6.18）；
  - 登录吞吐默认 2 哈希线程；更高并发调 `TETRIS_HASH_THREADS`（多 worker 时线程数按进程相乘），
    没有"按核数自动伸缩"；
  - 旧明文账号必须登录一次才会迁移（见 6.14）；
  - 远程压测脚本与手册已就绪（`play_remote.sh` / `REMOTE_BENCH.md`），尚未在真实云主机上跑过；
  - 连接数多机压测（`BENCH_REMOTE_CLIENTS`）修了源 IP 绑定，但 >6.4 万/台仍需加网卡 alias，未实测；
  - `output()` 的 1 MB 慢客户端上限是拍脑袋定的，未针对极端场景验证过；
  - 连接建立路径（accept/菜单）没有重测 -O2——它由内核 syscall 主导，`BENCH_OPT` 已支持，
    真要量就 `BENCH_OPT=-O0|-O2` 各跑一次 `bench.sh`。

## 8. 后续可选路线

| 方向 | 内容 | 价值 |
|---|---|---|
| 按 IP 兜底 | accept 时记录 peer IP（`User` +4B），宽阈值（如 300 次失败/5 分钟）挡住单源喷洒 | 中：退避只按用户名时的缺口 |
| 改密/重置 | 改密流程（复用 `PasswordPolicy`）+ 存量短密码的出路 | 低~中：看部署场景 |
| 连接池收缩 | 池子只涨不缩：加定期 trim（或降 `MAX_POOL_SIZE`），让峰值过后内存能还回去 | 低~中：长期运行/有突发峰值的部署 |
| `-flto` | 跨 TU 内联（UImanage/Utility 的小函数），预期几个点；用 bench.yml 的 A/B 步骤量 | 低~中：CPU |
| 连接建立路径 | accept 时 `showInitMenu` 也批量，并把固定菜单预渲染成静态字符串 | 低~中：10 万连接场景的 CPU（当前不是瓶颈） |
| 空闲定时器 | 维护"游戏中/待发/待清理"集合，别每 100ms 扫全表 | 低：只有继续压连接数才值得 |
| 远程压测 | 有第二台机器/云主机时用 `play_remote.sh` 验证 1/2/4 worker；30 万连接已证明卡在客户端 | 中：要证多核收益 |
| 质量工程（续） | 成绩落盘后的排行榜断言（需要稳定消行）、并发多连接交互 | 低：主链路已覆盖 |

已完成（不要再当待办）：`-O2`（4.7）、`User` 棋盘 int8（4.8）、连接池预创建 2048（4.9）、
**注册密码策略 + 未知账号假 PBKDF2**、**登录失败退避 + 哈希队列上限**（本轮）、
密码加盐哈希 + 迁移、登录吞吐、输出批量。

## 9. 给新会话的起步提示

```
读 docs/HANDOVER.md 了解现状，然后我们继续做 <方向>。

建议下一步：认证线还剩「按 IP 兜底」（退避只按用户名，换用户名喷洒仍能占满哈希线程）
和「改密/重置」；性能线已收口（两种场景服务端都不是瓶颈），别再堆优化。
`User` int8、池子 2048、-O2、注册密码策略、失败退避 + 队列上限都已完成，别重复做。
30 万连接已试过，卡在压测客户端（服务端 1.27GB/53% 单核），别在 4 核 runner 上硬加客户端进程。

要点：
- 编译/运行在 WSL 的 ~/Russia-Tetris（先 git pull --ff-only），Windows 侧仓库在 D:\dev\Russia-Tetris；
  WSL 克隆的 origin 是 HTTPS（偶尔 GnuTLS -110，重试即可）；WSL 里没有 SSH key，要 push 先从 Windows 拷；
- 沙箱里跑不了 WSL 和 g++，C++ 改动只能 push 到 main 让 CI 验证；失败细节靠 ::notice::/::error:: 注解回传；
- 但沙箱里有 Python：改测试脚本时先用 tests/mock_server.py 本地跑通，别浪费 CI 轮次
  （mock 的密码行为要和真服务端同步：哈希、迁移、策略、失败退避）；
- 压测数字的读法：CPU 看「游戏窗口 CPU / 每按键 µs」，「全程 CPU」被登录 PBKDF2 污染；
  内存看「服务端 RSS」，注意连接池预创建 2048 是固定 ~6MiB，且池子只涨不缩；
  跨 run/跨 runner 的绝对值不要直接相除，要比就用 bench.yml 的同机交错 A/B；
  跨提交 A/B 可以在临时 bench-run 分支上用 `git archive <base-sha>` 编一份旧二进制，
  再用 `BENCH_BIN` 指给 bench.sh / play_bench.sh（4.8/4.9 就是这么测的，
  注意 git archive 在子目录里只导出该目录，要 `-C` 回仓库根）；
- 起一次长时间压测前，先确认机器不会睡眠/合盖；否则 p99 和总耗时会完全失真；
- 不要再做这些已完成的事：WSL 安装、README 数据清理、GIF 处理、死代码清理、
  质量工程（集成/跨进程测试）、多核化（已实现且已有结论）、密码加盐哈希 + 迁移、
  登录吞吐优化（O(1) 查找 + 2 线程哈希池）、输出批量（CPU 已减半）、构建优化 -O2、
  `User` 棋盘 int8（sizeof 1984 / RSS -52%）、连接池预创建 2048（空闲 RSS 190→41MB）、
  **注册密码最小长度 + 未知账号假 PBKDF2（时序侧信道）**、
  **登录失败退避 + 哈希队列上限（含 auth_throttle_test）**。
```
