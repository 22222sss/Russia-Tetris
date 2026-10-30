# 远程压测手册（独立服务端）

> 目的：让**压测客户端和服务端跑在不同物理机**上，验证多 worker 是否真的带来扩展。
> 同机压测时两者抢同一批 CPU，多 worker 的收益会被完全掩盖（见 [HANDOVER 4.3](HANDOVER.md)）。

本手册以**国内云按量付费主机**为例（阿里云 / 腾讯云均可），一次实验通常几元到几十元。

## 1. 买机器

| 项 | 建议 | 说明 |
|---|---|---|
| 规格 | **4 vCPU / 8GB**，通用型或计算型 | 想测 4 worker 的 4 核扩展就选 4 核 |
| 镜像 | Ubuntu 22.04 / 24.04 LTS | 与 CI 一致 |
| 系统盘 | 40GB 起 | 代码 + 日志很小 |
| 公网 | 按使用流量计费，**峰值带宽 ≥ 200 Mbps** | 800 玩家时服务端下行约 15 MB/s（≈120 Mbps） |
| 安全组 | 入站放行 **TCP 9999**（最好只放行你家的出口 IP） | 端口可用 `--port` 改 |
| 计费 | 按量付费 | 跑完**释放实例和弹性公网 IP**，否则继续计费 |

**不要选**：突发性能型（t5/t6）、共享型、抢占式/竞价实例——CPU 配额或回收会让数据失真。

预算参考：4C8G 按量一般每小时几元以内；跑 2~3 小时 + 几 GB 流量，通常 ¥5~20。

## 2. 在 VPS 上准备

本地需要能**免密 ssh** 到 VPS（脚本用 `BatchMode=yes`，不会交互问密码）；没配的话先 `ssh-copy-id root@VPS_IP`。

```bash
sudo apt update
sudo apt install -y g++ make libevent-dev libspdlog-dev libfmt-dev libssl-dev python3
git clone https://github.com/22222sss/Russia-Tetris.git
cd Russia-Tetris/src && make -j"$(nproc)"
```

先确认网络可达（在本地 WSL 里跑，此时服务端还没起）：

```bash
python3 - <<'PY'
import socket
try:
    socket.create_connection(("VPS_IP", 9999), timeout=5)
    print("connected（服务已起）")
except ConnectionRefusedError:
    print("refused（网络可达，服务未起）")
except OSError as exc:
    print("不可达:", exc)
PY
```

- `refused`：说明包能到 VPS（只是还没起服务），安全组基本没问题；
- `不可达 / timed out`：检查安全组入站规则、VPS 防火墙（`ufw`/`iptables`）。

## 3. 一键跑

本地仓库（`~/Russia-Tetris`）执行：

```bash
cd ~/Russia-Tetris
bash tests/play_remote.sh --ssh root@VPS_IP --host VPS_IP \
    --players 800 --workers "1 2 4" --rounds 2
```

它会自动：

1. 在 VPS 上造 800 个哈希账号（约 30~60 秒）；
2. 按 `1 → 2 → 4` 起服务端、起 `sample_proc.py` 采样、本地 `play_test.py` 压测 30 秒；
3. 第 2 轮按 `4 → 2 → 1` 逆序复跑（抵消顺序效应）；
4. 取回采样 JSON，汇总成 `play-remote-results/<时间戳>/summary.md`，并打印每轮的 `::notice::` 行。

常用参数：

```bash
--players 800        # 800 玩家 × 50 按键/秒 = 4 万按键/秒 ≈ 2 核
--players 1600       # 4 万×2 ≈ 80k 按键/秒，用来压满 4 worker（下行约 30MB/s）
--seconds 30         # 游玩时长
--key-delay 0.02     # 每个玩家 50 次/秒
--rounds 2           # 1 表示只升序；2 表示升序 + 降序
--timeout 120        # 800 人登录（PBKDF2）需要更长时间
```

> 1600 玩家前先确认两件事：VPS 峰值带宽 ≥ 300Mbps，家里下行能跑 30MB/s；
> 否则瓶颈会先出现在网络上，而不是服务端 CPU。

## 4. 判读

`summary.md` 每行一组：

| 列 | 含义 |
|---|---|
| 就绪 | 成功登录并进入游戏的玩家数（< 玩家数说明登录/连接有问题） |
| 按键/秒 | 客户端实际发出的按键速率（应接近 玩家数 × 50） |
| 下行 KB/s | 服务端实际完成的工作量（整屏重绘的字节数） |
| 服务端 CPU% | 进程树合计**游戏窗口内 10 秒滑动平均的峰值**，**占满一核 = 100%**；单 worker 不可能超过 100% |
| RSS / fd / 进程数 | 进程数应约等于 worker 数 + 1（父进程） |

看两件事：

1. **同样负载下，加 worker 后 CPU 是否下降**（同样的按键量分摊到多核）；
2. **或同样负载下，按键/下行是否上升**（单 worker 扛不住时）。

如果加 worker 后 CPU 不降、吞吐不涨，先排除：

- 客户端先饱和：客户端 `keys_per_second` 明显低于 `玩家数×50`；
- 网络先饱和：VPS 峰值带宽/家中下行跑满；
- VPS 是超线程或共享核：看 `/proc/cpuinfo` 与 `%steal`（`top` 里的 `st`）；
- 服务端没打满一核：负载不够，加玩家或加按键频率。

## 5. 常见问题

- **登录超时**：800 个账号的 PBKDF2 在 4 核上约 15~30 秒，`--timeout` 保持 ≥120；
- **端口被占用**：脚本每轮前会 `pkill -x tetris_game`，只适用于专用压测机；
- **跑完没释放**：按量实例、弹性公网 IP、云盘都可能继续计费，实验后一并释放；
- **结果目录**：`play-remote-results/`（已 gitignore），含每轮 `play-*.json`、`samples-*.json`、`server-*.log`。

## 6. 顺便：远程连接数压测

`tests/bench.sh` 的 `BENCH_REMOTE_CLIENTS` 模式修正了源 IP 绑定（之前固定 `127.0.0.1`，远程连不上；**尚未在真实多机环境实测**）：

```bash
BENCH_REMOTE_CLIENTS="root@client1,root@client2" BENCH_HOST=VPS_IP \
  BENCH_WORKERS=4 bash tests/bench.sh 200000 10000 30 1 4
```

- 默认每台客户端绑定"到服务端路由会用的源 IP"；单机超过 6.4 万连接时，
  先给网卡加多个 IP（`ip addr add`），再用 `BENCH_REMOTE_BIND_IPS="ip1,ip2;ip3"` 覆盖；
- 10 万+ 连接对客户端 fd、端口、客户端 CPU 要求都很高；免费/低配客户端很容易先成为瓶颈。
