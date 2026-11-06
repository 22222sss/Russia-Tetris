# 部署与上线（systemd / Docker）

> 读者：要在一台 Linux 机器上把服务端跑成"能对外用"的产品的人。
> 前置条件：SSH 承载已完成（公网只开 sshd，9999 只绑回环），见 [SSH_DEPLOY.md](SSH_DEPLOY.md)。
> 本文件只讲**怎么装、怎么守、出事怎么查**；代码细节和历史坑见 [HANDOVER.md](HANDOVER.md)。

服务端是"一进程 + 事件循环"的形态，部署上真正会咬人的只有四件事，其余都是常规操作：

| 会咬人的点 | 不做的后果 | 本文位置 |
|---|---|---|
| **fd 上限** | systemd 默认的软限制就是连接数天花板，跑满后 `accept` 报 EMFILE，玩家看到"连不上" | 第 3、5 节 |
| **只绑回环** | 绕过 sshd 直连明文 9999，SSH 加密全白做 | 第 3、4、5 节 |
| **日志无限增长** | 跑几个月把磁盘写满，然后 SQLite 写失败 | 第 6 节 |
| **备份没人跑** | 忘密码没办法（没邮箱）、库坏了没退路 | 第 7 节 |

---

## 0. 上线 checklist（照顺序打勾）

每一条都给了"怎么验"，别只看"命令跑过了"。

**准备**

- [ ] 编译出两个二进制：`cd src && make -j"$(nproc)"` → `tetris_game`、`tetris-admin` 都在
- [ ] 部署形态选好：宿主机 systemd（推荐，第 3 节）或 Docker（第 4 节）
- [ ] 磁盘：库大小 ≈ 账号数 × 0.3 KB + 成绩数 × 0.1 KB；备份按"库大小 × 保留份数"预留
- [ ] 内存：每条 SSH 会话 = 1 个 sshd 会话进程 + 1 个 Python 桥（实测 **16.8~17 MB**）+ 服务端 3 KB；
      200 个并发玩家光桥就 ~3.4 GB（见 HANDOVER 6.36）

**部署**

- [ ] `sudo deploy/install-systemd.sh`（会建 `tetrisd` 账号、目录、单元、sysctl，并开机自启）
- [ ] **fd 上限生效**：`systemctl show tetris-game -p LimitNOFILE -p LimitNOFILESoft` → 两个都是 `300000`，
      再看进程里真正生效的值：
      `awk '/Max open files/' /proc/$(systemctl show tetris-game -p MainPID --value)/limits` → `300000 300000`
- [ ] **只绑回环**：`ss -ltn | grep 9999` → 只有 `127.0.0.1:9999`，没有 `0.0.0.0:9999` / `*:9999`
- [ ] `sudo deploy/install-ssh-bridge.sh` + `sudo passwd tetris`（登录账号与 `tetrisd` 分开）
- [ ] `sudo sshd -t` 通过；`sshd -T -C user=tetris | grep -i forcecommand` → `/usr/local/bin/tetris-bridge`
- [ ] 宿主机 sshd 全局建议：`MaxAuthTries 3`、`LoginGraceTime 20`（共享口令的第一道闸）

**验证**

- [ ] 从**另一台机器**：`ssh tetris@<IP>` → 直接进主菜单，方向键立刻生效，Ctrl+C 能退出
      （没有现成云主机时怎么造出"另一台机器"见第 12 节；自动化版是
      `python3 tests/ssh_play_test.py --host <IP> --key <私钥>`，9 条字节级判据）
- [ ] 再验一次明文端口**从外部连不上**：`nc -vz <IP> 9999` → 超时/拒绝（不是 accepted）
- [ ] 启动日志里有"生效配置"：`journalctl -u tetris-game -n 40`（二十来个环境变量的实际值）
- [ ] 崩溃自动拉起：`sudo kill -9 $(systemctl show tetris-game -p MainPID --value)` →
      2 秒后 `systemctl is-active tetris-game` 仍是 `active`
- [ ] 重启机器后服务自动起来：`sudo reboot` 后重跑上面两条检查（别只在装完那台会话里验证）

**运维**

- [ ] 日志轮转生效：`sudo ls -l /var/log/tetris` → `log.txt` + 可能的 `log.1.txt`…；总量不超过
      `TETRIS_LOG_MAX_BYTES × (TETRIS_LOG_MAX_FILES + 1)`（当前文件 + 历史，默认约 60 MiB）
- [ ] 备份 timer 已启用：`systemctl list-timers tetris-backup.timer` → 下一次 04:15
- [ ] **手动跑一次备份并检查产物**：`sudo systemctl start tetris-backup.service` →
      `sudo ls -l /var/backups/tetris`（600、非空）、`sudo journalctl -u tetris-backup -n 20`（integrity ok）
- [ ] **恢复演练过一次**（第 7.5 节，一条命令）：`sudo tetris-restore-drill /var/backups/tetris/<最新>.db`
      → 结尾必须是 `恢复演练通过`；再（可选）`--keep` 后用 `script` 起桥亲手玩一遍副本
- [ ] 记录本次上线的 commit：`git rev-parse HEAD`，写进你的运维记录（出问题才知道回滚到哪）

---

## 1. 两种形态：选哪个

| | systemd（第 3 节） | Docker（第 4 节） |
|---|---|---|
| 适用 | 一台自己的 VPS / 物理机 | 已有容器平台、或想和系统包隔离 |
| SSH 入口 | 宿主 sshd + 桥 | **同一个宿主的 sshd + 桥**（容器里只有服务端） |
| fd 上限 | 单元里的 `LimitNOFILE` | `--ulimit nofile=300000:300000` |
| 升级 | `install-systemd.sh` 重跑 / 换二进制 + `restart` | `docker compose up -d --build` |
| 备份 | `tetris-backup.timer` | `docker compose exec tetris tetris-backup`（或宿主 timer 调 docker exec） |

**推荐的组合**：服务端用 systemd，SSH 入口用宿主 sshd（两边都简单，排查路径最短）。
容器形态一样能上，只是"回环"这件事从 `TETRIS_BIND=127.0.0.1` 变成"发布地址必须写 `127.0.0.1`"
（见第 4.2 节，写错就是明文端口暴露）。

```
玩家 ──ssh──► sshd(宿主) ──ForceCommand──► tetris-bridge(宿主, 每会话 17MB)
                                              │ TCP 127.0.0.1:9999
                                              ▼
                                   tetris_game（systemd 服务 / 容器）
                                              │
                                              ▼
                                   /var/lib/tetris/tetris.db（WAL）
```

---

## 2. 前置条件

| 项 | systemd 形态 | Docker 形态 |
|---|---|---|
| 系统 | Ubuntu 22.04 / Debian 12（systemd ≥ 245） | 任何跑得动 Docker 的 Linux |
| 运行库 | 二进制动态链接 `libevent`/`spdlog`/`fmt`/`libcrypto`/`sqlite3` | 镜像里已带（`debian:bookworm-slim`） |
| Python 3 | 桥和恢复演练工具需要（`deploy/tetris-bridge.py`、`restore-drill.py`） | 宿主侧需要（桥不在容器里；演练可以直接用宿主装的工具） |
| 端口 | 22（sshd）、**9999 只对本机** | 同左；容器额外占用一个宿主回环端口 |
| 账号 | `tetrisd`（跑服务）、`tetris`（SSH 登录，只能跑桥） | 容器内 `tetris`（uid 10001） |
| 磁盘 | 库 + 日志（默认 ≤60 MiB）+ 备份（库大小 × 14） | 两个卷（`/data`、`/var/log/tetris`） |

编译依赖（只在构建机需要）：`build-essential libevent-dev libspdlog-dev libfmt-dev libssl-dev libsqlite3-dev`。

---

## 3. systemd 部署（推荐）

### 3.1 一键安装

```bash
cd src && make -j"$(nproc)" && cd ..     # 产出 tetris_game + tetris-admin
sudo deploy/install-systemd.sh           # 账号/目录/单元/sysctl/开机自启，幂等
sudo deploy/install-ssh-bridge.sh        # SSH 入口（公网只开 22）
sudo passwd tetris                       # 玩家用这个共享口令进来
```

`install-systemd.sh` 做的事（重跑安全，升级也用它）：

| 装到哪 | 内容 |
|---|---|
| `/usr/local/bin/tetris_game`、`tetris-admin`、`tetris-backup`、`tetris-restore-drill` | 两个二进制 + 备份脚本 + 恢复演练工具 |
| `/etc/systemd/system/tetris-game.service` 等三个单元 | 服务端 + 备份 service/timer |
| `/etc/sysctl.d/99-tetris.conf` | `somaxconn` / `tcp_max_syn_backlog` / `fs.file-max` |
| `/var/lib/tetris`（0700 `tetrisd`） | `tetris.db`（含 `-wal`/`-shm`），旧 `userdata.csv` 也放这里 |
| `/var/log/tetris`（0750 `tetrisd`） | `log.txt`，按大小轮转 |
| `/var/backups/tetris`（0700 `tetrisd`） | 每日备份（600） |

### 3.2 关键项：`LimitNOFILE=300000`

systemd 给服务的默认 nofile 通常是 **1024 软 / 524288 硬**（Ubuntu 22.04）。
服务端里 `SystemOptimizer` 只会把**软**限制抬到硬限制——硬限制本身是天花板，它救不了。
所以单元里把软硬都写死：

```ini
LimitNOFILE=300000:300000
```

验证（**上线必查**，这是最容易"看起来都在跑、其实只能接 1024 个连接"的地方）：

```bash
# systemd 把软/硬限制拆成两个属性，两个都应该是 300000
systemctl show tetris-game -p LimitNOFILE -p LimitNOFILESoft
# 进程里真正生效的值（比上面的"配置值"更可信）
PID=$(systemctl show tetris-game -p MainPID --value)
awk '/Max open files/' /proc/$PID/limits           # 300000  300000  files
```

`fs.file-max` 也要够（`deploy/sysctl.d/99-tetris.conf` 设成 1048576）。监听 backlog 用的是
65535，所以 `net.core.somaxconn` 也一并抬了——否则内核会把它截到默认的 4096。

> 容器形态对应的是 `--ulimit nofile=300000:300000`（compose 里写成 `ulimits.nofile`）。
> Docker 的默认值取决于宿主 `ulimit -n`，**不要指望它**。

### 3.3 目录与权限

- `tetrisd`：服务账号，拥有库和日志，**shell 是 nologin**（没人用它登录）；
- `tetris`：SSH 登录账号，只能通过 `ForceCommand` 跑桥，读不到 `/var/lib/tetris`
  （库里有密码哈希，两个账号必须分开，见 SSH_DEPLOY.md 第 2 节）；
- 单元里 `ProtectSystem=strict` + `StateDirectory`/`LogsDirectory`，服务端只能写自己的两个目录，
  `/usr`、`/home`、`/etc` 全只读。要临时放宽，先想清楚是不是在拿安全换方便。

### 3.4 验证

```bash
systemctl status --no-pager tetris-game
journalctl -u tetris-game -n 40 --no-pager      # 启动打印的"生效配置"在这里
ss -ltn | grep 9999                             # 127.0.0.1:9999
sudo tetris-admin list-users                    # 库能读（TETRIS_DB 默认 /var/lib/tetris/tetris.db? 见下）
```

`tetris-admin` 自己也认 `TETRIS_DB`，默认是**当前目录下的** `tetris.db`。
在宿主机上手工用时带上绝对路径最稳：

```bash
sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db tetris-admin stats
```

---

## 4. Docker 部署

镜像里**只有服务端和运维工具**（`tetris_game` / `tetris-admin` / `tetris-backup`），
SSH 入口（sshd + 桥）留在宿主——这样镜像不用碰宿主密钥、也不用 root 跑 sshd。

### 4.1 用 compose（推荐，`--network host`）

```bash
docker compose up -d --build
docker compose ps                      # STATUS 里应出现 (healthy)
docker compose logs -f tetris          # 启动打印的"生效配置"
docker compose exec tetris tetris-admin stats
```

compose 里已经写好的三件事：`network_mode: host`（容器直接绑宿主 `127.0.0.1:9999`，
和 systemd 形态完全一致）、`ulimits.nofile=300000`、`/data` 与 `/var/log/tetris` 两个卷。

### 4.2 用 `docker run`：发布地址必须写 `127.0.0.1`

```bash
docker run -d --name tetris \
  -p 127.0.0.1:9999:9999 \
  -e TETRIS_BIND=0.0.0.0 \
  -v tetris-data:/data -v tetris-logs:/var/log/tetris \
  --ulimit nofile=300000:300000 \
  --restart unless-stopped \
  russia-tetris
```

**为什么要 `TETRIS_BIND=0.0.0.0`**：`-p 127.0.0.1:9999:9999` 的 `docker-proxy` 连的是
**容器 IP**，不是容器自己的 loopback，容器里绑 `127.0.0.1` 会让宿主机连不进来。
所以"只绑回环"由宿主侧的发布地址保证：

| 发布写法 | 结果 |
|---|---|
| `-p 127.0.0.1:9999:9999` | ✅ 只有本机能连（桥能连、公网不能） |
| `-p 9999:9999` | ❌ `0.0.0.0:9999`，公网可以绕过 sshd 直连明文 |

用 `--network host` 时反过来：必须 `TETRIS_BIND=127.0.0.1`（compose 就是这么写的）。

### 4.3 HEALTHCHECK

镜像自带：

```dockerfile
HEALTHCHECK ... CMD bash -c 'exec 3<>/dev/tcp/127.0.0.1/${TETRIS_PORT:-9999} && head -c 16 <&3 >/dev/null'
```

它要求"连上 9999 并且**真的读到服务端主动推的首屏字节**"——只测端口 listen 会漏掉
"进程活着但事件循环卡死"。状态用 `docker ps` / `docker inspect -f '{{.State.Health.Status}}'` 看。

### 4.4 容器里的备份

```bash
docker compose exec tetris tetris-backup                # 默认写 /var/backups/tetris？见下
```

镜像里的 `tetris-backup` 用同一套环境变量，容器内默认 `TETRIS_BACKUP_DIR=/var/backups/tetris`
（不在卷里，容器删了就没了）。生产上建议把宿主的备份目录挂进来：

```yaml
    volumes:
      - tetris-data:/data
      - tetris-logs:/var/log/tetris
      - /var/backups/tetris:/var/backups/tetris     # 备份落到宿主
```

然后让**宿主**的 timer 调 `docker exec`（比容器内再跑一个 cron 好排查）：

```bash
# /etc/systemd/system/tetris-backup.service 的 ExecStart 换成：
#   /usr/bin/docker exec tetris tetris-backup
# 或者一行 crontab：
15 4 * * * root docker exec tetris tetris-backup >>/var/log/tetris-backup.log 2>&1
```

---

## 5. 环境变量与上限（部署视角）

完整的二十来个变量见 [HANDOVER.md 第 2 节](HANDOVER.md)，这里只列**部署时一定要过一遍**的：

| 变量 | 部署值 | 为什么 |
|---|---|---|
| `TETRIS_BIND` | `127.0.0.1`（systemd / host 网络）<br>`0.0.0.0`（`-p 127.0.0.1:...` 容器） | 明文端口只对本机的桥可见 |
| `TETRIS_PORT` | `9999` | 桥的默认目标；改了两边都要改（或设 `TETRIS_BRIDGE_TARGET`） |
| `TETRIS_DB` | `/var/lib/tetris/tetris.db`（容器 `/data/tetris.db`） | 绝对路径，避免"在别的目录一跑就建了个空库" |
| `TETRIS_LOG_DIR` | `/var/log/tetris` | 日志和库分家，`ProtectSystem=strict` 下也写得进去 |
| `TETRIS_LOG_MAX_BYTES` | `10485760`（10 MiB；`0`=不轮转） | 单份上限 |
| `TETRIS_LOG_MAX_FILES` | `5` | 历史份数（当前 `log.txt` 另算），默认 6 个文件 ≈ 60 MiB |
| `TETRIS_LOG_LEVEL` | `warn` | `info` 下每条连接都会落盘，别在生产常开 |
| `TETRIS_WORKERS` | `1`（默认） | 多 worker 时退避/IP 兜底/注册限流的状态是每进程一份，阈值实际 ×N（HANDOVER 6.23/6.25） |
| `TETRIS_AUTH_*` / `TETRIS_AUTH_IP_*` | 默认 | 共享口令入口的机器按"全站额度"重新评估（HANDOVER 6.35） |
| `TETRIS_REG_IP_MAX` / `_WINDOW_MS` | `20` / `300000` | 注册限流：单个 IP 5 分钟 20 次（`0` = 关这一维） |
| `TETRIS_REG_GLOBAL_MAX` / `_WINDOW_MS` | `200` / `3600000` | 注册限流：全进程 1 小时 200 次；**预期上线当天大量注册就把这个调大**（`0` = 关） |
| `TETRIS_ADMIN` / `TETRIS_BACKUP_DIR` / `TETRIS_BACKUP_KEEP` | 备份脚本用 | 单元里已设；`KEEP` 默认 14 |

改完变量先看**启动打印的生效配置**（`journalctl -u tetris-game -n 40`）——值全部取自各模块自己的
`configFromEnv()`，不是另抄的一份默认值。

---

## 6. 日志与轮转

### 6.1 为什么不用 logrotate

服务端用 spdlog 的异步文件 sink，**文件描述符是常开的**。外部 logrotate 的两种写法都有问题：

- `create`（rename 后新建）：进程继续写进已经改名的旧文件，新的 `log.txt` 永远是空的；
- `copytruncate`：截断后 spdlog 的写偏移还停在原处，文件前面会留下空洞（一屏幕 `NUL`）。

所以轮转放在**进程内**（`spdlog::rotating_file_sink`）：写满 `TETRIS_LOG_MAX_BYTES` 就改名为
`log.1.txt`，旧的依次后移，历史份数超过 `TETRIS_LOG_MAX_FILES` 后最老的删掉。Docker 和 systemd
两种形态都同样生效，不需要外部定时任务。

> 一个容易记错的细节：`TETRIS_LOG_MAX_FILES` 是**历史份数**（spdlog 的 `max_files`），
> 当前 `log.txt` 另算。所以默认 5 份 = 最多 6 个文件 ≈ 60 MiB，不是 50 MiB。

| 文件 | 说明 |
|---|---|
| `/var/log/tetris/log.txt` | 当前文件（多 worker 时是 `log.<pid>.txt`，每进程一份） |
| `/var/log/tetris/log.1.txt` … `log.5.txt` | 轮转出来的历史（默认最多 5 份） |

### 6.2 看日志

```bash
sudo -u tetrisd tail -f /var/log/tetris/log.txt        # 业务日志（默认只记 warn）
journalctl -u tetris-game -n 100 --no-pager            # 启动打印 + std::cout 步骤输出
sudo journalctl -u tetris-game --since "1 hour ago" | grep -i 'trim\|繁忙\|限流'
```

排查完记得把 `TETRIS_LOG_LEVEL` 改回 `warn`：`info` 下每建一条连接、每次登录成功都会写盘，
高峰时会变成"日志 IO 把自己的连接延迟拖高"。

---

## 7. 备份与恢复

### 7.1 自动备份

`tetris-backup.timer` 每天 **04:15**（+最多 10 分钟随机延迟、`Persistent=true` 会补跑
关机错过的那次）调用 `tetris-backup.service` → `/usr/local/bin/tetris-backup`：

1. `tetris-admin backup /var/backups/tetris/tetris-<UTC时间>.db`（SQLite **Online Backup API**，
   运行中也能拿到一致快照；WAL 模式下直接 `cp .db` 会丢数据，见 HANDOVER 6.31）；
2. 备份文件 `chmod 600`（里面是密码哈希），并做 `integrity_check` + 条数统计；
3. 按 `TETRIS_BACKUP_KEEP`（默认 14）删掉最老的；
4. 失败时删掉半成品并返回非 0 → systemd 记 `failed`，`systemctl --failed` 一眼能看到。

```bash
systemctl list-timers tetris-backup.timer              # 下次什么时候跑
sudo systemctl start tetris-backup.service             # 立刻跑一次
systemctl status tetris-backup.service                 # 上次结果（看 journal）
```

**备份不能只留在同一块盘上**：`/var/backups/tetris` 出了这台机器就没了。
至少要 `rsync`/`restic`/对象存储同步一份到别处，否则磁盘坏 = 玩家数据没了。

### 7.2 手工备份（任何形态）

```bash
sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db \
    tetris-admin backup /var/backups/tetris/manual-$(date +%F).db
# 容器：docker exec tetris tetris-admin backup /data/manual.db && docker cp ...
```

### 7.3 恢复前先确认

```bash
# 备份目录是 0700 tetrisd，所以下面的读操作都要 sudo
sudo sqlite3 /var/backups/tetris/tetris-20261102T041500Z.db "PRAGMA integrity_check;"
sudo sqlite3 /var/backups/tetris/tetris-20261102T041500Z.db \
  "SELECT COUNT(*) FROM accounts; SELECT COUNT(*) FROM scores;"
```

### 7.4 恢复（要点：先停服、连 WAL 一起清掉）

```bash
sudo systemctl stop tetris-game
sudo -u tetrisd cp /var/lib/tetris/tetris.db /var/lib/tetris/tetris.db.broken   # 留证
sudo rm -f /var/lib/tetris/tetris.db-wal /var/lib/tetris/tetris.db-shm          # 关键：别留着旧 WAL
sudo install -m 0600 -o tetrisd -g tetrisd \
     /var/backups/tetris/tetris-20261102T041500Z.db /var/lib/tetris/tetris.db
sudo systemctl start tetris-game
sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db tetris-admin stats
```

> 只覆盖 `.db` 而不清 `-wal`：SQLite 会把 WAL 里"更新但已不存在于库"的页回放上去，
> 恢复出来的数据可能既不是备份也不是现状。**这是恢复流程里最容易漏的一步。**
> 恢复完照着第 0 节再走一遍关键检查（尤其 `ss -ltn` 和 fd 上限）。
> 上面这套"真出事"的流程**不要**拿生产库练手；要验证备份能不能用，用下面的 7.5。

### 7.5 恢复演练（一条命令，不碰生产库）

"备份文件能打开"和"恢复之后能玩"是两件事：属主不对、`-wal` 没清、端口起不来、
这份备份其实是另一台机器的 —— 这些只有**真恢复一次**才会暴露。`tetris-restore-drill`
在一台已经装好的机器上，用临时目录 + 临时端口把整套流程走一遍：

```bash
sudo tetris-restore-drill /var/backups/tetris/tetris-20261102T041500Z.db
```

它做五步，每步打印 `ok`，任何一步失败都非 0 退出并**保留现场**：

1. 把备份复制到临时目录，`chown` 给服务账号、`chmod 600` —— 和真恢复一样的落盘方式；
2. `PRAGMA integrity_check` + 账号/成绩条数，并与 `/var/lib/tetris/tetris.db` 对比：
   副本比生产库还多 → 直接失败（说明这份备份不是这台机器上的）；
3. 用副本起一个**临时服务端**：另一个端口、另一个工作目录和日志目录，生产实例毫发无损；
4. 连上去确认首屏菜单真的发出来了（不是"进程在跑"就算数）；
5. 找到 `tests/integration_test.py` 时再跑一遍完整协议（12 项：注册 / 登录 /
   进游戏 / 改密 / 旧明文迁移……），证明这份数据确实能玩。它按两个位置找：
   `deploy/` 旁边的仓库副本，以及安装目录 `/usr/local/share/doc/tetris/tests/`
   （`install-systemd.sh` 会把 `integration_test.py` 和它 import 的
   `credential.py`/`db.py` 一起装过去）。两处都没有时会**打印"跳过协议测试"**
   —— 那时结尾仍是"恢复演练通过"，但只证明了"能起、能服务"，没证明"能玩"。

期望输出（中间几步省略）：

```
==> 2/5 检查副本：integrity_check + 条数（并与生产库对比）
  ok  integrity_check = ok；账号 128 个 / 成绩 3021 条
  ok  与生产库的差值符合预期：备份 128 账号/3021 成绩，生产 131/3104（备份之后的新数据，恢复会丢掉这部分）
...
  ok  协议测试通过（注册/登录/进游戏都不依赖生产库）

恢复演练通过：这份备份可以直接拿去恢复（integrity ok、临时实例能服务、完整协议通过）。
```

**亲手玩一遍副本**（脚本替不了"终端里真的能操作"这一段）：

```bash
# 1) 演练后保留临时实例，记下打印的端口（比如 41234）和 pid
sudo tetris-restore-drill <备份文件> --keep

# 2) 在当前终端直接玩这个副本（桥跑在服务器侧，所以要 sudo -u tetris + 环境变量；
#    从你笔记本 ssh 时传的环境变量 sshd 默认不收，见排错表最后一条）
sudo -u tetris TETRIS_BRIDGE_TARGET=127.0.0.1:41234 \
    script -q -c /usr/local/bin/tetris-bridge /dev/null

# 3) 玩完停掉临时实例
sudo kill <脚本打印的 pid>
```

注意：

- 工具由 `install-systemd.sh` 装到 `/usr/local/bin/tetris-restore-drill`；
- 它**只读**生产库（对比条数），不改生产库、不需要停服；临时目录默认在 `/tmp` 下，
  演练成功即删（失败会保留并在输出里给出路径）；
- `--server-cmd` 只是调试用（可以指向 `tests/mock_server.py`），真机别用；
- 演练通过 ≠ 恢复流程没问题：第 7.4 节那几步（停服 → 挪走旧库 → 删 `-wal`/`-shm` →
  放备份 → `chown tetrisd` → 起服）仍然要按顺序手工做，工具只替你证明**文件本身能恢复**；
- 建议**每次升级前**、以及至少每月一次，在备份 timer 跑完之后演练一遍（几十秒）。

---

## 8. 升级与回滚

### 8.1 升级

```bash
sudo systemctl start tetris-backup.service       # 1) 先备份（一条命令，别省）
git pull --ff-only && cd src && make -j"$(nproc)" && cd ..
sudo deploy/install-systemd.sh                   # 2) 换二进制 + restart（幂等）
journalctl -u tetris-game -n 40                  # 3) 确认"生效配置"没变、listen 成功
sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db tetris-admin stats
```

数据库结构由 `Db.cpp` 里的 `CREATE TABLE IF NOT EXISTS` 维护，**升级不需要手工迁移**；
旧 `userdata.csv` 只会在"空库 + 文件存在"时导入一次（HANDOVER 6.31）。

### 8.2 回滚

数据库是向后兼容的（新版本不删列、不改格式），所以回滚**只是换回旧二进制**：

```bash
sudo systemctl stop tetris-game
sudo install -m 0755 /path/to/old/tetris_game /usr/local/bin/tetris_game
sudo systemctl start tetris-game
```

需要连数据一起回滚时才用第 7.4 节（这时会丢掉备份之后的新数据，先想清楚）。
所以每次上线前记下 `git rev-parse HEAD`，回滚才有明确目标。

---

## 9. 日常巡检（一分钟版）

```bash
systemctl is-active tetris-game && systemctl is-active tetris-backup.timer
ss -ltn | grep 9999                                  # 必须只有 127.0.0.1:9999
sudo ls -l /var/log/tetris | tail -3                 # 轮转有没有在走（目录 0750 tetrisd）
sudo ls -lt /var/backups/tetris | head -3             # 最新备份是不是今天的（目录 0700）
systemctl --failed                                    # 备份/服务有没有 failed
sudo -u tetrisd env TETRIS_DB=/var/lib/tetris/tetris.db tetris-admin stats
df -h /var/lib/tetris /var/backups/tetris             # 库和备份的盘都别超过 80%
```

玩家反馈类问题的入口：

| 现象 | 先看 |
|---|---|
| 连上就断 / 提示 `not available` | `chsh -s /bin/sh tetris`；见 SSH_DEPLOY.md 排错表 |
| 提示"游戏服务暂时不可用" | 服务端活没活、桥的目标端口对不对 |
| 所有人被"登录尝试过于频繁" | 来源 IP 映射是否生效（桥的日志）、`TETRIS_AUTH_IP_MAX_FAILURES` |
| 玩家反馈"注册过于频繁" | 正常现象也可能是额度太紧：单 IP 20 次/5 分钟、全局 200 次/小时（`TETRIS_REG_*`）；看 `log.txt` 里"注册过于频繁（全局额度/单 IP 额度）"那行 |
| 登录突然变慢 | 哈希队列（`TETRIS_AUTH_QUEUE_LIMIT`）、有没有人在刷注册 |

---

## 10. 排错表

| 现象 | 原因 | 处理 |
|---|---|---|
| `systemctl start` 立刻失败，`bind Error` | 9999 被手工启动的旧实例占着，或 `TETRIS_BIND` 写错 | `ss -ltnp \| grep 9999` 找到占用者；`TETRIS_BIND` 只能是 IPv4 字面量 |
| 服务起来但玩家连不上 | `TETRIS_BIND` 是 `127.0.0.1` 而桥在别的机器/容器里 | 桥必须和服务端在同一网络命名空间；容器形态看第 4.2 节 |
| 大量 `accept` 报 `Too many open files` | `LimitNOFILE` 没生效（改了单元没 `daemon-reload`） | `systemctl daemon-reload && systemctl restart tetris-game`，再按 3.2 节验证 |
| 只能接一千来个连接 | 软限制还是 1024（手工 `./tetris_game` 跑在 shell 里，没走单元） | 用单元启动；或 `ulimit -n 300000` 后再跑 |
| 启动就报"无法打开日志文件" | `/var/log/tetris` 不存在或属主不对 | `install -d -m 0750 -o tetrisd -g tetrisd /var/log/tetris` |
| `log.txt` 还是越来越大 | `TETRIS_LOG_MAX_BYTES=0`（被谁设成了不轮转） | 看启动打印的"生效配置"，改回 `10485760` |
| 备份 `failed`，报"找不到数据库" | `TETRIS_DB` 指向别处（脚本在 `/var/lib/tetris` 以外的地方跑） | 单元里已经写死，手工跑时用绝对路径 |
| 备份 `failed`，报"目标文件已存在" | 同一秒跑了两次（时间戳撞了） | 脚本会自动加 `-1` 后缀；撞到说明有人在手工并发跑，别这么干 |
| Docker 一直 `unhealthy` | 容器绑在了 `127.0.0.1` 而 healthcheck 用宿主网络除外的情况 / 进程卡死 | `docker logs tetris`、`docker exec tetris ss -ltn`；看是不是事件循环起来了 |
| `docker stop` 要等 10 秒 | 没有 init（PID 1 不处理默认动作的信号） | compose 里有 `init: true`；`docker run` 加 `--init`，或依赖服务端自己装的 SIGTERM 处理器 |
| 重启后服务没起来 | 只 `start` 没 `enable` | `systemctl enable tetris-game tetris-backup.timer` |
| 演练时想玩副本，`TETRIS_BRIDGE_TARGET=... ssh tetris@host` 没反应 | 环境变量是给**服务器侧**的桥用的；sshd 默认不收客户端环境变量（`AcceptEnv` 是关的） | 在服务器上起桥：`sudo -u tetris TETRIS_BRIDGE_TARGET=127.0.0.1:<端口> script -q -c /usr/local/bin/tetris-bridge /dev/null`（见 7.5）；或临时把 `ForceCommand` 加上 `--target 127.0.0.1:<端口>`，`sshd -t` 后 reload |
| `tetris-restore-drill` 报"读不到备份" | 备份是 0600、目录 0700（service 账号） | 用 `sudo` 跑；非 root 演练只能读自己的文件 |
| `tetris-restore-drill` 报"副本比生产库还多" | 这份备份不是这台机器的（或生产库被换过） | 先确认备份来源；别拿它去恢复 |

---

## 11. 安全边界（部署完再过一遍）

- **公网只开 22**：`9999` 只能出现在 `127.0.0.1`（容器形态看发布地址）；防火墙层面也把 9999 拒掉，
  比只靠 `TETRIS_BIND` 稳；
- **两个账号**：`tetrisd`（跑服务、能读库）与 `tetris`（SSH 登录、只能跑桥）分开；
- **共享口令是"入口闸"不是身份**：真正的身份是游戏账号 + 三层限流；公网机器建议
  `MaxAuthTries 3` + fail2ban（sshd 会记录失败）；
- **库文件权限**：`tetris.db*` 0600/0700，备份 600，别放进 web 目录；
- **`ForceCommand` 不是沙箱**（SSH_DEPLOY.md 第 8 节）：登录账号本身要穷；
- 想再收紧就先关掉容器里不必要的能力（`--read-only --tmpfs /tmp`、`cap_drop: ALL`），
  再验证第 0 节那几条——**加固的前提是"还能玩"**。

---

## 12. 没有云主机时，怎么验"公网边界"

第 0 节里有两条**必须由另一台机器发出**的验证：从另一台机器 `ssh tetris@<IP>` 能不能直接
进游戏、`<IP>:9999` 从外面通不通。它们验的不是代码，是**出网边界**，所以机器必须在
公网的另一侧。没有现成云主机时，下面两条路都能真验（2026-11-06 都跑过），适用面不同：

| 路 | 前提 | 证明力 |
|---|---|---|
| 12.1 家用宽带 + 公网 IPv6 | 运营商给全球 IPv6、路由器不挡入站 | **强**：这台机器本身就在公网上，`22` 通 / `9999` 不通与云主机同构 |
| 12.2 公共 TCP 中继 + 一台公网 CI runner | 只要有一台能出网的公网机器 | **中**：证明"公网→sshd→桥→游戏"整条链路可用，**不**证明安全组/入站策略 |

两条都**不用改仓库代码**，验证工具是 `tests/ssh_play_test.py`（9 条字节级判据）。

### 12.1 家用宽带 + 公网 IPv6（Windows 宿主 + WSL 形态）

很多家用宽带给的是全球可路由 IPv6（`2408:`/`2409:`/`240e:` 开头）。这时**这台笔记本
本身就在公网上**，不需要云主机。WSL2 在 Windows 10 上只有 NAT（`networkingMode=mirrored`
要 Win11 22H2+），所以要在 Windows 侧把公网 IPv6 的 22 转进 WSL：

```powershell
# 管理员 PowerShell
# 1) 只放行入站 TCP 22（9999 一律不放行 —— 这本身就是"公网只能到 22"的证据）
New-NetFirewallRule -DisplayName 'tetris-public-ssh22' -Direction Inbound `
    -Action Allow -Protocol TCP -LocalPort 22 -Profile Any

# 2) 公网 IPv6:22 -> WSL 里的 sshd:22（地址变了就重跑；先 delete 再 add，幂等）
$wslIp = (wsl.exe -d Ubuntu-22.04 hostname -I) -split '\s+' | Select-Object -First 1
netsh interface portproxy delete v6tov4 listenaddress=:: listenport=22
netsh interface portproxy add    v6tov4 listenaddress=:: listenport=22 `
    connectaddress=$wslIp connectport=22
```

先在本机对**自己的公网地址**自检一遍链路（真外部还得靠另一台机器）：

```powershell
Test-NetConnection 2408:8459:... -Port 22      # 期望 True
Test-NetConnection 2408:8459:... -Port 9999    # 期望 False
```

坑：

- **WSL 的 IPv4 每次重启都可能变**，转发规则要按"现值"重设；写成幂等脚本最省事；
- `New-NetFirewallRule` / `netsh add` 要管理员；`netsh ... show`、`netstat -ano` 普通用户也能跑；
  **`Get-Process` 可能看不到别的用户建的监听**（`netstat` 里有 PID、`Get-Process` 说没有），
  别据此判断"没有进程在监听"；
- **运营商/路由器可能挡入站 IPv6**：那时 22 从外面也是 timeout。别把这个当成服务端问题——
  先看 12.2 能不能连通，再回头查路由器。

### 12.2 公共 TCP 中继 + 一台公网 CI runner

没有公网 IPv6（或路由器不让入站）时，可以让任何一台公网机器当探针，用公共 TCP 中继
把入口搬出去：

```
GitHub runner ──公网──► serveo（匿名中继） ──隧道──► 本机一次性 sshd ──► bridge ──► 127.0.0.1:9999
```

```bash
# 1) 一次性探针 sshd：只监听回环、只认一次性密钥、Match 里只跑桥（生产 sshd 配置一字不改）
#    Port 2222 / ListenAddress 127.0.0.1 / PasswordAuthentication no /
#    AuthorizedKeysFile /var/lib/tetrisprobe/authorized_keys / Match User tetris: ForceCommand tetris-bridge
sudo /usr/sbin/sshd -f /tmp/probe_sshd.conf -E /tmp/probe_sshd.log

# 2) 公网中继（匿名策略：随机端口、10 分钟、2 分钟无流量关、256 KiB/s）
ssh -R 0:localhost:2222 serveo.net
#   Forwarding TCP connections from serveousercontent.com:31165

# 3) 在**外部机器**上跑验收
python3 tests/ssh_play_test.py --host serveousercontent.com --port 31165 --key ~/.ssh/probe
```

坑：

- **别把生产 sshd 直接挂出去**：匿名中继是公开端口，生产 `tetris` 账号是共享口令。
  用一次性 sshd + 一次性密钥，验完 `kill` 掉；
- 密钥要放 **`tetris` 自己的 0700 目录**（如 `/var/lib/tetrisprobe`）。放 `/tmp` 下必然
  `Authentication refused: bad ownership or modes for directory /tmp` —— sshd 的 StrictModes
  会检查 `authorized_keys` 的**每一级目录**，而 `/tmp` 全世界可写；
- `sshd -T` 不带 `-C user=tetris` 时看不到 `Match User` 里的 `ForceCommand`，会显示
  `forcecommand none`，别误判"配置没生效"；
- 用脚本驱动终端时，**本地这端的 pty 也要 `tty.setraw`**，否则 `\x1b[B` 会被行缓冲压住、
  服务端永远收不到（`integration_test.py` 走 socket，碰不到这个坑）；CI runner 上没有 tty，
  还要显式 `TIOCSWINSZ` 设成 80x24，否则远端 pty 可能是 0x0。

### 12.3 这轮的实测（2026-11-06，WSL + 家用宽带）

| 项 | 结果 |
|---|---|
| 公网 IPv6 自检（`2408:8459:...`） | `:22` 可连（经 `v6tov4` → WSL sshd）、`:9999` 拒绝 |
| Windows 侧暴露面 | 入站只放行 TCP 22；`portproxy` 只有 `::22 -> <WSL>:22`；`9999` 只有 `127.0.0.1`（属本机转发进程） |
| 外部探针（GitHub runner，出口 `48.214.54.115`） | 中继 `serveousercontent.com:31165`（`37.27.37.254`）TCP 可连 |
| 经中继 ssh 进游戏 | **9/9**：首屏菜单 → 注册 → 登录 → 进游戏 → 方向键（无回车即生效）→ 断开自退；15.6 s / 4134 字节 |
| 从 runner 打公网 IPv4 的 9999 | `timed out`（信息性，见下） |

同一轮还补了 Docker 形态（WSL 里真跑）：镜像 128 MB、`HEALTHCHECK` 约 8 s 变 healthy、
容器内 `uid 10001` / `nofile 300000` / 监听 `0.0.0.0:9999`，宿主只发布 `127.0.0.1:10099`、
`docker stop` 244 ms（compose 形态 93 ms）、穿过桥玩容器 **9/9**、
容器里 `tetris-admin stats` 与账密落库都正常（详见 `docs/HANDOVER.md` 第 5 节）。

**这次没验到的**（确实没有条件，别当成已经验过）：

- **云平台安全组/防火墙**：12.1 验的是"主机自己只开 22"，云上还要看安全组；
- **从 IPv6 外部机器 `nc -vz <公网IPv6> 9999`**：需要一台 IPv6 可达的外部机器，
  这台网络里没有（GitHub runner 只有 IPv4，公共端口探测服务大多只支持 IPv4）；
- **云主机上的 Docker 形态**：容器只在 WSL 里跑过。

### 12.4 副作用：源 IP 会被"抹平"

任何挡在 sshd 前面的 TCP 转发——`netsh portproxy`、公共中继、云负载均衡——都会让 sshd
看到的对端地址**变成同一个**，于是桥用 `SSH_CLIENT` 映射出来的"每玩家专属伪源地址"也
退化成同一个，`IpThrottle`（每 IP 300 次/5 分钟）和注册限流（每 IP 20 次/5 分钟）
**实际变成"全站一份额度"**（设计取舍见 HANDOVER 6.35）。用中继验过之后，这些阈值要按
"全站额度"重新估。直连云主机的 sshd（客户端 IP 直达）没有这个问题。

> 中国网络下 `docker build` 还会卡在拉基础镜像：`registry-1.docker.io` i/o timeout，
> 需要配镜像加速器（`/etc/docker/daemon.json` 的 `registry-mirrors`）或
> `docker pull <mirror>/library/debian:bookworm` 再 `docker tag`。CI 的 runner 在墙外，
> **不会**暴露这个问题。
