#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""部署形态的自动化检查（Dockerfile / systemd 单元 / 备份脚本 / 文档）。

为什么需要它：上线前的这些文件**平时没人跑**——Dockerfile 只在真部署时才 build，
systemd 单元只在真机器上才被解析。写错一个字符，CI 全绿、上线当天才发现。
所以这里把"改坏了会怎样"提前变成一条条断言：

1. systemd 单元的关键项：LimitNOFILE=300000:300000（连接数的隐藏天花板）、
   TETRIS_BIND=127.0.0.1（否则明文端口直接上公网）、Restart=always（崩溃能回来）、
   User 不是 root、日志/数据目录分离、加固项还在；
2. 备份单元 + timer：ExecStart 指向真实存在的脚本、Unit= 对得上、OnCalendar/Persistent、
   ReadWritePaths 放行了备份目录；
3. **环境变量拼写**：单元/Dockerfile 里写的每个 TETRIS_* 都必须在 src/ 里被真正
   getenv() 到——把 TETRIS_LOG_MAX_BYTES 写成 TETRIS_LOG_MAXSIZE 不会报错，
   只会让"轮转"悄悄不生效；
4. Dockerfile：多阶段、运行阶段非 root、HEALTHCHECK 真的连 9999 读字节、基础镜像钉版本、
   运行阶段不装 -dev 包；
5. .dockerignore 不把 tetris.db / log.txt / 旧 CSV（都含密码哈希）打进构建上下文；
6. docker-compose.yml：host 网络 + TETRIS_BIND=127.0.0.1 的组合自洽，
   ulimits/nofile 与 systemd 的 LimitNOFILE 对齐；
7. deploy/*.sh 的语法，以及备份脚本的两条行为：失败不留半成品、按 KEEP 清理旧备份；
8. docs/DEPLOY.md 存在，且包含每个环境变量的说明（文档和单元不漂移）。

用法（仓库根目录或任意位置都行，路径按脚本位置推算）：
    python3 tests/deploy_test.py
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 这些脚本级变量不属于服务端代码（备份脚本 / 安装脚本自己用），
# 所以"必须在 src/ 里被 getenv"这条对它们不适用。
SCRIPT_ONLY_ENV = {
    "TETRIS_ADMIN",
    "TETRIS_BACKUP_DIR",
    "TETRIS_BACKUP_KEEP",
    "TETRIS_BIN_DIR",
}


class Failure(Exception):
    """断言失败。"""


def annotate(level, message):
    msg = str(message).replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::%s::%s" % (level, msg), flush=True)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def repo_path(*parts):
    return os.path.join(REPO_ROOT, *parts)


# ---------------------------------------------------------------------------
# systemd 单元解析（只认 Key=Value 和 [Section]，够用就行，不引 ini 库）
# ---------------------------------------------------------------------------

def parse_unit(path):
    """{section: [(key, value), ...]}；同一 key 可能多次出现（Environment 就是这样）。"""
    sections = {}
    current = None
    for raw in read(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
            sections.setdefault(current, [])
            continue
        if current is None or "=" not in line:
            continue
        key, value = line.split("=", 1)
        sections[current].append((key.strip(), value.strip()))
    return sections


def unit_values(sections, section, key):
    return [value for (k, value) in sections.get(section, []) if k == key]


def unit_value(sections, section, key, default=None):
    values = unit_values(sections, section, key)
    return values[-1] if values else default


def unit_env(sections):
    """把 Environment=KEY=VALUE 收成 dict（systemd 里同名的后者覆盖前者）。"""
    result = {}
    for value in unit_values(sections, "Service", "Environment"):
        # 允许 Environment="A=1" "B=2" 这种写法，虽然本仓库只用了单键一行
        for token in re.findall(r'[^\s"]+', value):
            if "=" in token:
                key, val = token.split("=", 1)
                result[key] = val.strip('"')
    return result


# ---------------------------------------------------------------------------
# 各项检查
# ---------------------------------------------------------------------------

def test_game_unit(sections):
    if unit_value(sections, "Service", "User") in (None, "root", "0"):
        raise Failure("tetris-game.service 的 User 必须是普通账号（不能是 root/未设置）")
    limit = unit_value(sections, "Service", "LimitNOFILE")
    if limit != "300000:300000":
        raise Failure("LimitNOFILE 必须是 300000:300000（systemd 默认值会成连接数天花板），"
                      "实际是 %r" % limit)
    env = unit_env(sections)
    if env.get("TETRIS_BIND") != "127.0.0.1":
        raise Failure("单元里必须 TETRIS_BIND=127.0.0.1，否则明文 9999 直接暴露在公网；"
                      "实际是 %r" % env.get("TETRIS_BIND"))
    if unit_value(sections, "Service", "Restart") not in ("always", "on-failure"):
        raise Failure("缺少 Restart=always：崩溃/OOM 后不会自己起来")
    exec_start = unit_value(sections, "Service", "ExecStart")
    if not exec_start or not exec_start.startswith("/"):
        raise Failure("ExecStart 必须是绝对路径：%r" % exec_start)
    if not exec_start.endswith("/tetris_game"):
        raise Failure("ExecStart 应该指向 tetris_game，实际 %r" % exec_start)

    # 数据和日志分家：库在 StateDirectory，日志在 LogsDirectory。
    # 否则 ProtectSystem=strict 会让服务写不进去（或者逼着人关掉加固）。
    if unit_value(sections, "Service", "StateDirectory") != "tetris":
        raise Failure("StateDirectory 应该是 tetris（/var/lib/tetris 放库）")
    if unit_value(sections, "Service", "LogsDirectory") != "tetris":
        raise Failure("LogsDirectory 应该是 tetris（/var/log/tetris 放 log.txt）")
    db_path = env.get("TETRIS_DB", "")
    if not db_path.startswith("/var/lib/tetris/"):
        raise Failure("TETRIS_DB 应该落在 StateDirectory 里（/var/lib/tetris/...），实际 %r" % db_path)
    log_dir = env.get("TETRIS_LOG_DIR", "")
    if log_dir.rstrip("/") != "/var/log/tetris":
        raise Failure("TETRIS_LOG_DIR 应该是 /var/log/tetris，实际 %r" % log_dir)
    if not db_path.endswith(".db"):
        raise Failure("TETRIS_DB 看起来不像库文件：%r" % db_path)

    # 日志轮转：这两个变量缺了就会退回"只涨不缩"的老行为
    if env.get("TETRIS_LOG_MAX_BYTES", "0") in ("", "0"):
        raise Failure("TETRIS_LOG_MAX_BYTES 不能是 0（那等于不轮转，log.txt 会一直增长）")
    if int(env.get("TETRIS_LOG_MAX_FILES", "0")) < 2:
        raise Failure("TETRIS_LOG_MAX_FILES 至少 2，否则永远只有当前那一个文件")

    # journal 里要能看见启动打印的"生效配置"（排查时唯一的一手信息）
    for key in ("StandardOutput", "StandardError"):
        if unit_value(sections, "Service", key) != "journal":
            raise Failure("%s=journal 缺失：启动打印的生效配置会丢" % key)

    # 加固项：这几条是"部署时顺手就有的"，删掉不会立刻出问题，但会一点点放宽
    hardened = {
        "NoNewPrivileges": "yes",
        "ProtectSystem": "strict",
        "ProtectHome": "yes",
        "PrivateTmp": "yes",
        "RestrictAddressFamilies": "AF_INET AF_INET6 AF_UNIX",
    }
    for key, expected in hardened.items():
        actual = unit_value(sections, "Service", key)
        if actual != expected:
            raise Failure("加固项 %s 应为 %s（实际 %r）—— 服务端只需要读写自己的两个目录"
                          % (key, expected, actual))
    return "LimitNOFILE=%s，绑定 127.0.0.1，日志轮转 %s B × %s 份" % (
        limit, env.get("TETRIS_LOG_MAX_BYTES"), env.get("TETRIS_LOG_MAX_FILES"))


def test_backup_units(timer_sections, service_sections):
    exec_start = unit_value(service_sections, "Service", "ExecStart")
    if not exec_start:
        raise Failure("tetris-backup.service 没有 ExecStart")
    base = os.path.basename(exec_start)
    candidates = [repo_path("deploy", base), repo_path("deploy", base + ".sh")]
    script = next((path for path in candidates if os.path.isfile(path)), None)
    if script is None:
        raise Failure("备份单元的 ExecStart 指向 %s，但仓库里没有 %r 对应的脚本"
                      % (exec_start, [os.path.basename(p) for p in candidates]))
    if unit_value(service_sections, "Service", "Type") != "oneshot":
        raise Failure("备份是跑完就退的任务，Type 必须是 oneshot")
    if unit_value(service_sections, "Service", "User") in (None, "root", "0"):
        raise Failure("备份单元也应该用服务账号跑（库文件属主是它）")

    env = unit_env(service_sections)
    backup_dir = env.get("TETRIS_BACKUP_DIR", "")
    if not backup_dir.startswith("/"):
        raise Failure("TETRIS_BACKUP_DIR 必须是绝对路径：%r" % backup_dir)
    rw_paths = unit_values(service_sections, "Service", "ReadWritePaths")
    if not any(backup_dir.rstrip("/") == path.rstrip("/") for path in rw_paths):
        raise Failure("ProtectSystem=strict 下必须 ReadWritePaths=%s，否则备份写不进去" % backup_dir)
    if backup_dir.rstrip("/").startswith("/var/lib/tetris"):
        raise Failure("备份不能和库放同一个目录：磁盘写满时会把库一起带走")

    if unit_value(timer_sections, "Timer", "Unit") != "tetris-backup.service":
        raise Failure("timer 的 Unit= 必须是 tetris-backup.service")
    calendar = unit_value(timer_sections, "Timer", "OnCalendar")
    if not calendar or calendar == "daily":
        raise Failure("OnCalendar 应写明确的时刻（如 *-*-* 04:15:00），实际 %r" % calendar)
    if unit_value(timer_sections, "Timer", "Persistent") != "true":
        raise Failure("Persistent=true 缺失：关机错过的那次不会补跑")
    if unit_value(timer_sections, "Install", "WantedBy") != "timers.target":
        raise Failure("timer 的 [Install] WantedBy 必须是 timers.target")
    return "每天 %s 备份到 %s，保留 %s 份" % (
        calendar, backup_dir, env.get("TETRIS_BACKUP_KEEP"))


def dockerfile_logical_lines():
    """把 Dockerfile 的反斜杠续行拼成逻辑行（保留原始行号用于报错）。"""
    lines = []
    buffer = ""
    for raw in read(repo_path("Dockerfile")).splitlines():
        stripped = raw.rstrip()
        if stripped.endswith("\\"):
            buffer += stripped[:-1] + " "
            continue
        buffer += stripped
        lines.append(buffer)
        buffer = ""
    if buffer:
        lines.append(buffer)
    return lines


def test_dockerfile():
    path = repo_path("Dockerfile")
    if not os.path.isfile(path):
        raise Failure("仓库根目录没有 Dockerfile")
    text = read(path)
    logical = dockerfile_logical_lines()
    froms = [line.split()[1] for line in logical if line.strip().upper().startswith("FROM ")]
    if len(froms) < 2:
        raise Failure("Dockerfile 必须是多阶段构建（当前 FROM 只有 %r）" % froms)
    for image in froms:
        if image.endswith(":latest") or ":" not in image:
            raise Failure("基础镜像要钉版本（如 debian:bookworm），不要用 %r" % image)

    users = [line.split(None, 1)[1].strip() for line in logical
             if line.strip().upper().startswith("USER ")]
    if not users:
        raise Failure("Dockerfile 没有 USER：容器会以 root 跑")
    if users[-1].split(":")[0] in ("root", "0"):
        raise Failure("最后一个 USER 不能是 root（当前 %r）" % users[-1])

    if not re.search(r"^HEALTHCHECK\b", text, re.MULTILINE):
        raise Failure("缺少 HEALTHCHECK：容器「活着」和「能玩」是两件事")
    health = [line for line in logical if line.strip().upper().startswith("HEALTHCHECK ")]
    if not health or "/dev/tcp/127.0.0.1" not in health[0]:
        raise Failure("HEALTHCHECK 必须真的连 127.0.0.1:9999（只查进程存在会漏掉卡死）")
    if "EXPOSE 9999" not in text:
        raise Failure("缺少 EXPOSE 9999")
    if "VOLUME" not in text:
        raise Failure("缺少 VOLUME /data：库会随容器一起丢")

    for binary in ("tetris_game", "tetris-admin"):
        if not re.search(r"COPY --from=build \S*/build/%s " % re.escape(binary), text):
            raise Failure("运行阶段没有从构建阶段拷贝 %s" % binary)

    # 运行阶段不该带 -dev 包（体积 + 攻击面），构建阶段才需要
    runtime_part = text.split(froms[1], 1)[1] if len(froms) > 1 else ""
    dev_packages = re.findall(r"\b\w+-dev\b", runtime_part)
    if dev_packages:
        raise Failure("运行阶段装了 -dev 包：%r" % sorted(set(dev_packages)))
    return "%d 个阶段，USER=%s，HEALTHCHECK 连 127.0.0.1:9999" % (len(froms), users[-1])


def test_dockerignore():
    path = repo_path(".dockerignore")
    if not os.path.isfile(path):
        raise Failure("缺少 .dockerignore：本地二进制/日志/数据库会被打进构建上下文")
    patterns = set()
    for raw in read(path).splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            patterns.add(line)
    required = [
        "src/tetris_game",    # 用旧代码编的二进制不能盖到镜像里
        "src/obj",
        "src/tetris.db",      # 含密码哈希
        "src/log.txt",
        "src/userdata.csv",   # 迁移前的明文密码
    ]
    missing = [pattern for pattern in required if pattern not in patterns]
    if missing:
        raise Failure(".dockerignore 少了：%r" % missing)
    return "%d 条忽略规则（二进制 / obj / 库 / 日志 / 旧 CSV）" % len(patterns)


def test_compose():
    path = repo_path("docker-compose.yml")
    if not os.path.isfile(path):
        raise Failure("缺少 docker-compose.yml")
    text = read(path)
    if "network_mode: host" not in text:
        raise Failure("compose 用 host 网络（配 TETRIS_BIND=127.0.0.1）才有意义；"
                      "换 bridge 网络时发布地址必须写 127.0.0.1")
    if "TETRIS_BIND: 127.0.0.1" not in text:
        raise Failure("compose 里 TETRIS_BIND 必须是 127.0.0.1（host 网络下等于宿主回环）")
    if not re.search(r"nofile:\s*\n\s*soft:\s*300000\s*\n\s*hard:\s*300000", text):
        raise Failure("compose 的 ulimits.nofile 必须和 systemd 的 LimitNOFILE 对齐（300000）")
    for needle in ("tetris-data:/data", "tetris-logs:/var/log/tetris", "restart: unless-stopped"):
        if needle not in text:
            raise Failure("compose 缺少：%s" % needle)
    return "host 网络 + 127.0.0.1 + nofile=300000 + 两个卷"


def test_env_names_exist_in_code():
    """单元/Dockerfile 里写的 TETRIS_* 必须真的被 src/ 读到。"""
    keys = set()
    for unit in ("tetris-game.service", "tetris-backup.service"):
        sections = parse_unit(repo_path("deploy", "systemd", unit))
        keys.update(unit_env(sections).keys())
    for line in dockerfile_logical_lines():
        stripped = line.strip()
        if stripped.upper().startswith("ENV "):
            for token in stripped.split()[1:]:
                if "=" in token:
                    keys.add(token.split("=", 1)[0])

    sources = []
    for base, _, files in os.walk(repo_path("src")):
        if os.sep + "obj" in base:
            continue
        for name in files:
            if name.endswith((".cpp", ".h")):
                sources.append(os.path.join(base, name))
    blob = "\n".join(read(path) for path in sources)

    unknown = []
    for key in sorted(keys):
        if key in SCRIPT_ONLY_ENV:
            continue
        if '"%s"' % key not in blob:
            unknown.append(key)
    if unknown:
        raise Failure("这些环境变量在 src/ 里没有被 getenv 到（拼错了？）：%r" % unknown)
    return "%d 个环境变量都能在代码里找到对应 getenv" % len(keys)


def make_workdir(tag):
    """建一个可写的临时目录。

    正常环境就是 tempfile；但 DSH 这类沙箱里 TMP 目录可能"能 mkdir 不能建文件"，
    所以候选依次是 TETRIS_TEST_TMPDIR、系统临时目录、仓库根目录（用点号前缀，
    已进 .gitignore），每个都真的写一个探针文件再决定用不用。
    """
    candidates = []
    override = os.environ.get("TETRIS_TEST_TMPDIR", "")
    if override:
        candidates.append(override)
    candidates.append(tempfile.gettempdir())
    candidates.append(REPO_ROOT)
    for base in candidates:
        try:
            os.makedirs(base, exist_ok=True)
            # 不用 tempfile.mkdtemp：它按 0700 建目录，而某些受限环境（DSH 沙箱）
            # 会拒绝往"子进程刚建出来的 0700 目录"里写文件。用默认权限建完再收紧。
            path = os.path.join(base, ".tetris-deploy-test-%s-%d-%d"
                                % (tag, os.getpid(), time.time_ns() % 1000000))
            os.makedirs(path)
            if os.name != "nt":
                os.chmod(path, 0o700)
            probe = os.path.join(path, "probe")
            with open(probe, "w", encoding="utf-8") as handle:
                handle.write("x")
            os.unlink(probe)
            return path
        except OSError:
            continue
    raise Failure("找不到可写的工作目录（试过 %r）" % candidates)


def shell_works(shell):
    """真正跑一次才算数：Windows 上 which("bash") 常指向 WSL 启动器，它跑不起来。"""
    try:
        proc = subprocess.run([shell, "-c", "exit 0"], capture_output=True, timeout=20,
                              encoding="utf-8", errors="replace")
        return proc.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def find_shell():
    """找一个能用的 POSIX shell 来跑 deploy/*.sh。

    CI 上是 /bin/sh（dash，正好帮我们验"脚本没写 bash 专属语法"）；
    本地（Windows）没有可用的 sh，可以设 TETRIS_TEST_SH 指向 PortableGit 的 bash。
    """
    override = os.environ.get("TETRIS_TEST_SH", "")
    candidates = [override] if override else []
    for name in ("sh", "bash"):
        path = shutil.which(name)
        if path:
            candidates.append(path)
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and shell_works(candidate):
            return candidate
    return None


def run_shell(shell, args, env=None, timeout=120):
    kwargs = {"capture_output": True, "text": True,
              "encoding": "utf-8", "errors": "replace", "timeout": timeout}
    if env is not None:
        kwargs["env"] = env
    return subprocess.run([shell] + args, **kwargs)


def test_shell_syntax():
    shell = find_shell()
    if not shell:
        return "跳过（本机没有 sh/bash；设 TETRIS_TEST_SH 指向一个 POSIX shell 可启用）"
    scripts = [repo_path("deploy", name) for name in sorted(os.listdir(repo_path("deploy")))
               if name.endswith(".sh")]
    for script in scripts:
        proc = run_shell(shell, ["-n", script])
        if proc.returncode != 0:
            raise Failure("%s 语法错误：%s" % (os.path.basename(script), proc.stderr.strip()))
    return "%d 个脚本 %s -n 通过" % (len(scripts), os.path.basename(shell))


def test_backup_script_behaviour():
    shell = find_shell()
    if not shell:
        return "跳过（本机没有 sh；设 TETRIS_TEST_SH 可启用）"
    script = repo_path("deploy", "tetris-backup.sh")
    workdir = make_workdir("backup")
    db_path = os.path.join(workdir, "tetris.db")
    with open(db_path, "wb") as handle:
        handle.write(b"SQLite format 3\x00" + b"\x00" * 4096)
    backup_dir = os.path.join(workdir, "backups")

    fake = os.path.join(workdir, "fake-tetris-admin")
    with open(fake, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("#!/bin/sh\n"
                     "if [ \"${FAKE_MODE:-ok}\" = \"fail\" ]; then exit 1; fi\n"
                     "printf 'x' > \"$2\"\n")
    os.chmod(fake, 0o755)

    env = dict(os.environ)
    env.update({
        "TETRIS_ADMIN": fake,
        "TETRIS_DB": db_path,
        "TETRIS_BACKUP_DIR": backup_dir,
        "TETRIS_BACKUP_KEEP": "2",
        "FAKE_MODE": "ok",
    })
    env.pop("TETRIS_LOG_LEVEL", None)

    def run(mode):
        run_env = dict(env)
        run_env["FAKE_MODE"] = mode
        return run_shell(shell, [script], env=run_env)

    try:
        for _ in range(3):
            proc = run("ok")
            if proc.returncode != 0:
                raise Failure("备份脚本正常路径退出码 %d：%s"
                              % (proc.returncode, (proc.stderr or proc.stdout).strip()[-400:]))
            # 时间戳精确到秒：等一秒，让"保留 2 份"的排序是确定的
            time.sleep(1.1)

        files = sorted(os.listdir(backup_dir))
        if len(files) != 2:
            raise Failure("KEEP=2 时应只剩 2 份，实际 %d 份：%r" % (len(files), files))

        before = set(os.listdir(backup_dir))
        proc = run("fail")
        if proc.returncode == 0:
            raise Failure("tetris-admin backup 失败时，脚本必须返回非 0（systemd 才会记 failed）")
        after = set(os.listdir(backup_dir))
        if after - before:
            raise Failure("备份失败后留下了半成品：%r" % sorted(after - before))
        return "失败不留半成品；KEEP=2 时清理到 2 份"
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def test_docs():
    path = repo_path("docs", "DEPLOY.md")
    if not os.path.isfile(path):
        raise Failure("缺少 docs/DEPLOY.md（部署指南 + 上线 checklist）")
    text = read(path)
    needles = {
        "LimitNOFILE": "300000",
        "TETRIS_BIND": "127.0.0.1",
        "备份": "tetris-backup",
        "回滚/升级": "回滚",
        "checklist": "checklist",
    }
    missing = [label for label, needle in needles.items() if needle not in text]
    if missing:
        raise Failure("docs/DEPLOY.md 里找不到：%r" % missing)

    keys = set()
    for unit in ("tetris-game.service", "tetris-backup.service"):
        keys.update(unit_env(parse_unit(repo_path("deploy", "systemd", unit))).keys())
    undocumented = [key for key in sorted(keys)
                    if key not in SCRIPT_ONLY_ENV and key not in text]
    if undocumented:
        raise Failure("docs/DEPLOY.md 没有说明这些环境变量：%r" % undocumented)

    # 安装脚本引用的每个单元文件都必须真的存在（改名时最容易漏）
    installer = read(repo_path("deploy", "install-systemd.sh"))
    for name in ("tetris-game.service", "tetris-backup.service", "tetris-backup.timer"):
        if name not in installer:
            raise Failure("install-systemd.sh 没有安装 %s" % name)
        if not os.path.isfile(repo_path("deploy", "systemd", name)):
            raise Failure("deploy/systemd/%s 不存在" % name)
    return "DEPLOY.md 覆盖 %d 个环境变量，安装脚本的单元文件都在" % len(keys)


CHECKS = []


def main():
    game_sections = parse_unit(repo_path("deploy", "systemd", "tetris-game.service"))
    timer_sections = parse_unit(repo_path("deploy", "systemd", "tetris-backup.timer"))
    backup_sections = parse_unit(repo_path("deploy", "systemd", "tetris-backup.service"))

    checks = [
        ("systemd 单元：服务端", lambda: test_game_unit(game_sections)),
        ("systemd 单元：备份 + timer", lambda: test_backup_units(timer_sections, backup_sections)),
        ("环境变量都真的被代码读到", test_env_names_exist_in_code),
        ("Dockerfile", test_dockerfile),
        (".dockerignore", test_dockerignore),
        ("docker-compose.yml", test_compose),
        ("deploy/*.sh 语法", test_shell_syntax),
        ("备份脚本行为", test_backup_script_behaviour),
        ("docs/DEPLOY.md 与单元一致", test_docs),
    ]

    print("仓库: %s" % REPO_ROOT, flush=True)
    failed = 0
    for name, fn in checks:
        started = time.time()
        try:
            detail = fn()
        except Failure as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "部署形态检查失败 - %s: %s" % (name, exc))
        except OSError as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc), flush=True)
            annotate("error", "部署形态检查失败 - %s: %s" % (name, exc))
        else:
            suffix = ("：%s" % detail) if detail else ""
            print("PASS %s (%.1fs)%s" % (name, time.time() - started, suffix), flush=True)

    total = len(checks)
    if failed:
        print("部署形态检查失败: %d/%d 项" % (failed, total), flush=True)
        return 1

    print("部署形态检查全部通过: %d/%d 项" % (total, total), flush=True)
    annotate("notice", "部署形态检查全部通过（%d 项）" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
