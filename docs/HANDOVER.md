# 交接文档（HANDOVER）

> 给“下一个会话/下一个人”看的现状说明。读完这份就不需要翻历史聊天记录。

## 1. 仓库与本地环境

| 项 | 位置 |
|---|---|
| 仓库 | https://github.com/22222sss/Russia-Tetris |
| 本地克隆 | `D:\dev\Russia-Tetris`（Windows 侧，CRLF） |
| Linux 侧克隆 | `/home/ly/Russia-Tetris`（WSL，LF，编译用这份） |
| Git | `D:\dev\tools\PortableGit`（已加入用户 PATH） |
| 全局 git 配置 | `D:\dev\.gitconfig`，通过用户环境变量 `GIT_CONFIG_GLOBAL` 指向 |
| SSH key | `D:\dev\.ssh\id_ed25519`（无口令），`known_hosts` 预置 GitHub 官方指纹 |
| SSH 包装器 | `D:\dev\tools\ssh-github.exe`（源码 `ssh-github.cs`） |
| WSL 发行版 | Ubuntu-22.04，磁盘在 `D:\wsl\Ubuntu-22.04\ext4.vhdx`（约 2.3 GB） |
| 游戏客户端 | `D:\dev\tools\tetris_client.py` |

说明：

- 所有 Git 相关内容都在 **D 盘**，C 盘只有 WSL 的系统组件（约 2 GB，卸载 WSL 才能释放）；
- 沙箱里 `sh.exe` 无法创建信号管道，所以 `core.sshCommand` 指向自编译的 `ssh-github.exe`，由它直接调用 Windows 自带 OpenSSH；
- 提交身份：`22222sss <1348291944@qq.com>`。

## 2. 构建与运行（WSL）

```bash
sudo apt update && sudo apt install -y build-essential libevent-dev libspdlog-dev libfmt-dev telnet
cd ~/Russia-Tetris/src && make -j"$(nproc)"
./tetris_game          # 监听 0.0.0.0:9999

# 可调项（都是环境变量）：
TETRIS_PORT=8888 ./tetris_game            # 换端口
TETRIS_WORKERS=4 ./tetris_game            # 多进程：4 个 worker 抢同一端口（SO_REUSEPORT）
TETRIS_LOG_LEVEL=debug ./tetris_game      # 打开 debug 日志（会逐条 flush）
```

客户端（Windows cmd）：

```cmd
chcp 65001
wsl -d Ubuntu-22.04 -- python3 /mnt/d/dev/tools/tetris_client.py
```

- 用 `tetris_client.py` 而不是 `telnet`：telnet 默认行模式，方向键要按回车才发，且回车不是 CRLF；
- 退出客户端：`Ctrl+C`；
- 账号和成绩存在服务端工作目录的 `userdata.csv`。

## 3. 压测工具链

| 文件 | 作用 |
|---|---|
| `tests/load_test.py` | 压测客户端：`--host --port --pid --json --min-success --bind-ips`；失败按 timeout/refused/reset/fd 分类；输出 p50/p95/p99 与每 10 秒速率 |
| `tests/bench.sh` | 一键：调 `ulimit -n 400000`、sysctl、编译、起服务、采样、分片压测、合并报告 |
| `tests/sample_proc.py` | 独立采样服务端 RSS/fd/CPU（写 `server_samples.json`） |
| `tests/merge_results.py` | 合并多分片 JSON 与采样数据，生成 `result.json` |
| `tests/integration_test.py` | 协议级集成测试（仅标准库）：注册 → 登录 → 排行榜 → 选难度 → 进游戏 → 按键 → 游戏结束，外加二次登录、错误密码、快速重连；失败时发 `::error::` 注解 |
| `tests/mock_server.py` | 协议 mock，**只用于在本地验证集成测试脚本本身**（沙箱里跑不了真服务端时），不是协议规范 |
| `tests/cross_process_test.py` | 跨进程存储一致性：两个服务进程共享同一份 `userdata.csv`，验证 A 注册的账号能在 B 登录（多进程化的前置验证） |
| `.github/workflows/build.yml` | 每次 push：编译 + 协议级集成测试 + 200 连接冒烟 + bench.sh 100 连接冒烟 + `sizeof(User)` |
| `.github/workflows/bench.yml` | 手动触发（或 push 到 `bench-run` 分支）跑大压测，结果作为 Artifact |

用法：

```bash
bash tests/bench.sh 100000 5000 30 1 2      # 10 万 / 双客户端
bash tests/bench.sh 200000 10000 30 1 4     # 20 万 / 四客户端
```

## 4. 已跑出的数据（GitHub Actions，4 核 / 16 GB）

| 指标 | 10 万 / 单客户端 | 10 万 / 双客户端 | 20 万 / 四客户端 |
|---|---|---|---|
| 成功建立 | 70,674（70.7%） | **100,000（100%）** | **172,891（86.5%）** |
| 失败 | 29,326（全 timeout） | 0 | 27,109（全 timeout） |
| 总耗时 | 457 s | 124 s | 248 s |
| 速率 | 155/s | 804/s | 697/s |
| 服务端 CPU | 19.9% | 18.5% | 58.4% |
| 服务端 RSS | 681.6 MB | 771.1 MB | 1376.2 MB |
| 服务端 fd 峰值 | 83,292 | 99,737 | 180,959 |

结论：瓶颈在**压测端**（单进程客户端），服务端 17 万连接时 CPU 58%，仍有单核余量。

## 5. 代码里改过的关键点

- `SystemOptimizer::setFileDescriptorLimit()`：目标软限制 300000，**只升不降**；
- 移除无用的 `sys/epoll.h`、`sys/timerfd.h`、`iconv.h` 与 `utf8_to_gbk()`；
- `UImanage::clear()` 用 `\x1b[2J\x1b[H`（7 字节）；
- 首次运行自动创建空 `userdata.csv`；
- 删除 `User::getAllUsers()`：`forEachUser` 锁内遍历替代后已无调用点，留着容易被误用（它按值复制整张 map，正是上次性能事故的根因）；
- **修复连接静默（集成测试发现）**：连接结束时从不释放 `struct event`。内核在 `close()` 时已把 fd 从 epoll 摘除，而 libevent 仍认为已注册，导致新连接复用到同一 fd 时 `event_add` 不再 `epoll_ctl(ADD)` —— 该连接永远收不到可读事件。现在 `registerFdEvent` 注册前先 `event_del`+`event_free` 同 fd 旧事件（`fd < 0` 的定时器事件除外），并新增 `EventLoop::removeFdEvent` 在连接结束时调用；
- `User::addUser` 改用 `insert_or_assign`：原 `emplace` 在 fd 复用窗口（死连接尚未被 100ms 清理）会插入失败且返回值被忽略，留下僵尸映射；
- 新增 `TETRIS_LOG_LEVEL` 环境变量（默认 `warn`）按需打开 debug 日志；
- 新增 `TETRIS_PORT` 环境变量覆盖监听端口（默认 9999），方便同机跑多实例与跨进程测试；
- **多进程 worker**：`TETRIS_WORKERS=N`（默认 1）。N>1 时父进程只负责 fork / 转发信号 / 回收；每个 worker 各自建 logger（`log.<pid>.txt`）、连接池（预创建数按 N 均分）、事件循环，并用 `SO_REUSEPORT` 独立 bind 同一端口，内核按连接做负载均衡。worker 之间不共享内存，共享状态只有 `userdata.csv`（flock 保护）。因为每个 worker 仍是单线程，`User` 那套无锁假设**不需要改**；
- **存储层做成多进程安全**（多核化的前置）：`userdata.csv` 的读-改-写放进 `flock` 临界区（跨进程互斥、同线程可重入），写整表时仍是"临时文件 + rename"；缓存改为按文件 `mtime`/`size` 失效，否则别的进程注册的账号在本进程看不到。锁文件是 `userdata.csv.lock`；
- **修复发送路径丢字节**：`output()` 原来只判断 `send` 返回 -1/0，"部分写"和 EAGAIN 都被当成成功，高负载或慢客户端下会静默丢数据。现在每个 `User` 带发送缓冲（`sendbuf` + `sendmutex` + `pendingoutput`），`flushUserOutput()` 写多少算多少，剩下的由 100ms 定时器（`EventLoop::unregister_Event_User`）补发；缓冲超过 1 MB 视为死连接并关闭；
- `showRecentScores` 现在能识别 `Read_recent_grades` 的哨兵值 `"1"`（否则新账号点"我最近的成绩"会看到一个孤零零的 `1`）；
- **异步文件回调收回事件循环线程**：`loadPlayerDataAsync` / `readRecentGradesAsync` / `readAllPlayerInfoAsync` 的回调原来跑在异步文件线程上，却直接操作 `User`（`setStatus`/`setReceivedata`/`showLoadMenu`），与事件循环线程竞争同一批字段（data race）。现在统一用 `EventLoop::post()` 经**自管道**投递回循环线程执行——没用 `evthread_use_pthreads()`，避免给每个 event 操作加全局锁；回调里还加了"状态已变化就丢弃"的守卫，防止连接断开/被复用后仍往旧连接渲染；
- makefile 加 `-MMD -MP` + `-include $(DEPS)`：改头文件会正确触发依赖它的 `.cpp` 重编。此前规则只依赖 `.cpp`，改 `User.h` 会留下按旧布局编译的 `.o`（结构体大小不一致，运行期会内存错乱）。CI 里有一步用 `touch User.h` + `make -n` 验证它没退化；
- README 只保留**已复测**的数据（原 32 核 126 GB 的数字已删）。

## 6. 已知坑

1. **telnet 不能用**：行模式 + 回车不是 CRLF → 用 `tetris_client.py`；
2. **CRLF**：`/mnt/d` 里的 makefile 带 `\r` 会编译失败 → 在 `~/` 编译，或 `sed -i 's/\r$//'`；
3. **GitHub Actions 日志**：公开仓库下载日志需要 admin token；调试靠 `::notice::` / `::error::` 注解回传；
4. **README 图片**：GitHub 对单图有大小限制，大 GIF 压到 <10 MB（`*-demo.gif`）；
5. **沙箱限制**：pwsh 跑在低完整性，`wsl` 命令会 `E_ACCESSDENIED`；MSYS 的 `sh.exe` 无法创建信号管道；
6. **WSL 里的 `wsl` 命令**：不要在 Ubuntu 里执行，要在 Windows PowerShell 里执行；
7. **沙箱里没有本地验证能力**：`wsl` 会 `E_ACCESSDENIED`，也没有 `g++` 和可用的 `python`（只有 WindowsApps 占位 stub）。改代码只能 push 到 `main` 让 GitHub Actions 验证（`build.yml` 只在 push `main` 或 PR 时触发），结果用 GitHub API 读 run conclusion + annotations；日志全文读不到，**所以 CI 里任何失败细节都必须用 `::error::` 注解输出**，否则只能看到"某步红了"。
8. **服务端日志默认只记 `warn`**：调试时用 `TETRIS_LOG_LEVEL=debug ./tetris_game`，会自动开启逐条 flush（异步 sink 不 flush 的话进程被杀会丢日志尾部）。日志写到工作目录下的 **`log.txt`**，不是 stdout。
9. **GitHub API 的 check-runs 端点数据会滞后**：需要实时步骤状态时查 `/repos/:o/:r/actions/jobs/:job_id`；另外 job 的 "Complete job" 步骤结束前 annotations 是空的，要等它完成再读。

## 7. 仓库现状

- `main` 干净，CI 全绿；无 tag；
- `bench-run` 分支已删除（如需触发 bench：重建该分支并 push）；
- 远程另有 `Russia-Tetris_1.0` 分支（历史版本，未维护）；
- 许可证：MIT（`LICENSE`）；
- 遗留：远程压测代码无文档、`userdata.csv` 明文密码。

## 8. 后续可选路线

| 方向 | 内容 | 价值 |
|---|---|---|
| 质量工程 | ✅ 已覆盖：注册/登录/排行榜（最近成绩、全服 top）/选难度/进游戏/按键/游戏结束/错误密码/快速重连，并接入 CI | 已建立回归保护 |
| 密码安全 | `userdata.csv` 明文 → 加盐哈希 + 迁移 | 中：部署前提 |
| 服务端多核化 | ✅ 已实现多进程 worker（`TETRIS_WORKERS=N` + `SO_REUSEPORT`，存储层 flock 保护）。**待办**：用 `bench.yml` 实测 1/2/4 worker 的扩展性并补进 README | 高：README 里点名的瓶颈 |
| 远程压测 | 用 `BENCH_REMOTE_CLIENTS` 跑多机，目标 30 万 | 中：需多台机器 |

## 9. 给新会话的起步提示

```
读 docs/HANDOVER.md 了解现状，然后我们继续做 <方向>。
注意：编译/运行在 WSL 的 ~/Russia-Tetris，Windows 侧仓库在 D:\dev\Russia-Tetris；
不要再做 WSL 安装、README 数据清理、GIF 处理、死代码清理这些已完成的事。
```
