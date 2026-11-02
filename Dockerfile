# Russia-Tetris 服务端镜像（多阶段、非 root、带 HEALTHCHECK）
#
#   docker build -t russia-tetris .
#   docker run -d --name tetris \
#     -p 127.0.0.1:9999:9999 \
#     -v tetris-data:/data -v tetris-logs:/var/log/tetris \
#     --ulimit nofile=300000:300000 \
#     russia-tetris
#
# 镜像里**只有服务端**（tetris_game + tetris-admin）：SSH 入口（sshd + tetris-bridge）
# 留在宿主上，公网只开 22，容器只把 9999 发布到宿主的 127.0.0.1。
# 为什么这样切、以及 `--network host` 的另一种形态：docs/DEPLOY.md 第 2、4 节。
#
# 两处"改了就上不了线"的细节：
#   * 运行阶段只装运行库（-dev 包不进镜像），二进制 strip 过；
#   * 回环绑定不能靠容器内的 TETRIS_BIND —— 见下方 ENV 注释。

# ---------------------------------------------------------------------------
# 构建阶段：和 CI 用同一组依赖，编出 tetris_game + tetris-admin
# ---------------------------------------------------------------------------
FROM debian:bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libevent-dev \
        libspdlog-dev \
        libfmt-dev \
        libssl-dev \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY src/ /build/

# -O2 是 makefile 的默认值（CI 的优化级别守卫会守住它），这里不覆盖。
# 注意不要在这里跑 tetris_game 做"冒烟"：它没有 --version 之类的参数，
# 直接启动就会监听并阻塞，构建会卡死 —— 冒烟放到运行阶段（HEALTHCHECK）。
RUN make -j"$(nproc)" \
    && strip --strip-unneeded tetris_game tetris-admin \
    && ./tetris-admin --help > /dev/null

# ---------------------------------------------------------------------------
# 运行阶段：只带运行库 + 非 root 账号
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS runtime

LABEL org.opencontainers.image.title="russia-tetris" \
      org.opencontainers.image.description="Multiplayer terminal Tetris server (loopback-only, SSH-fronted)" \
      org.opencontainers.image.licenses="MIT"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libevent-2.1-7 \
        libspdlog1.10 \
        libfmt9 \
        libssl3 \
        libsqlite3-0 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 tetris \
    && useradd --system --uid 10001 --gid 10001 --home-dir /data \
            --no-create-home --shell /usr/sbin/nologin tetris \
    && install -d -m 0700 -o tetris -g tetris /data \
    && install -d -m 0750 -o tetris -g tetris /var/log/tetris

COPY --from=build /build/tetris_game /usr/local/bin/tetris_game
COPY --from=build /build/tetris-admin /usr/local/bin/tetris-admin
COPY deploy/tetris-backup.sh /usr/local/bin/tetris-backup
COPY docs/DEPLOY.md /usr/local/share/doc/tetris/DEPLOY.md
COPY docs/SSH_DEPLOY.md /usr/local/share/doc/tetris/SSH_DEPLOY.md

RUN chmod 0755 /usr/local/bin/tetris_game /usr/local/bin/tetris-admin /usr/local/bin/tetris-backup

# 容器内绑 0.0.0.0：`-p 127.0.0.1:9999:9999` 的 docker-proxy 连的是**容器 IP**，
# 不是容器自己的 loopback，绑 127.0.0.1 会导致宿主机连不进来。
# 于是"只绑回环"这件事由宿主侧完成：发布端口时必须写 `-p 127.0.0.1:9999:9999`
# （写成 `-p 9999:9999` 就是公网明文端口，SSH 承载白做）。
# 用 `--network host` 时反过来：设 TETRIS_BIND=127.0.0.1，和 systemd 形态一致。
ENV TETRIS_BIND=0.0.0.0 \
    TETRIS_PORT=9999 \
    TETRIS_DB=/data/tetris.db \
    TETRIS_LOG_DIR=/var/log/tetris \
    TETRIS_LOG_MAX_BYTES=10485760 \
    TETRIS_LOG_MAX_FILES=5 \
    TETRIS_LOG_LEVEL=warn

WORKDIR /data
VOLUME ["/data"]
EXPOSE 9999
USER tetris

# 健康检查要证明"服务真的能说话"，不能只证明"端口在 listen"：
# 连上后必须读到服务端主动推的首屏菜单字节（16 字节足够跨过 ANSI 前缀）。
# 读不到 = 事件循环卡死 / 绑错地址，都会在 docker ps 里显示 unhealthy。
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD bash -c 'exec 3<>/dev/tcp/127.0.0.1/${TETRIS_PORT:-9999} && head -c 16 <&3 >/dev/null'

CMD ["/usr/local/bin/tetris_game"]
