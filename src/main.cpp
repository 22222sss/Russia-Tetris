#include"../Common/Common.h"
#include"../TetrisGame/User.h"
#include"../TetrisGame/Game.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../TetrisGame/Server.h"
#include"../TetrisGame/AuthThrottle.h"
#include"../TetrisGame/IpThrottle.h"
#include"../TetrisGame/RegThrottle.h"
#include"../TetrisGame/Db.h"
#include"../EventLoop/EventLoop.h"
#include"../TetrisGame/Filedata.h"
#include"../TetrisGame/PlayerInfo.h"
#include"../Utility/SystemOptimizer.h"
#include <spdlog/async.h>
// 轮转 sink 和它的工厂函数 rotating_logger_mt 都在 sinks/ 下：顶层
// spdlog/rotating_file_sink.h 那个转发头在 Ubuntu 24.04 的 libspdlog-dev(1.12)
// 里已经不存在，写错路径在 CI 上就是 fatal error（见 HANDOVER 6.50）
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <sys/wait.h>
#include <arpa/inet.h>   // inet_pton：TETRIS_BIND 解析（netinet/in.h 不保证带上它）

// 全局 logger。
// 注意：异步 logger 内部带线程池，fork 出来的子进程里并没有那些线程，
// 所以不能在静态初始化阶段创建 —— 必须由每个进程（含每个 worker）自己建一份。
std::shared_ptr<spdlog::logger> logger;

// 创建 spdlog::logger 对象（异步日志，避免高并发下同步写盘阻塞主线程）
// 通过初始化函数配置：大队列 + 2 个写线程，避免队列溢出阻塞；生产环境只记录 warn 及以上
//
// 调试时可用环境变量 TETRIS_LOG_LEVEL=debug/info 打开更详细的日志：
// 异步 sink 默认不逐条 flush，进程被杀会丢尾部，所以非默认级别下开启 flush_on。
// 多 worker 时每个进程写自己的 log.<pid>.txt，避免 N 个进程交织同一个文件。
//
// 日志文件**按大小轮转**（TETRIS_LOG_MAX_BYTES × TETRIS_LOG_MAX_FILES）：以前 log.txt
// 只涨不缩，一个跑上几个月的实例能把磁盘写满；改用外部 logrotate 又和 spdlog 的
// 常开 fd 冲突（rename 后继续写旧 inode，copytruncate 则会在文件里留下空洞），
// 所以轮转放在进程内做 —— Docker 和 systemd 两种形态都同样生效。
static std::string logDirectoryFromEnv() {
    const char* raw = std::getenv("TETRIS_LOG_DIR");
    if (raw == nullptr || *raw == '\0') {
        return std::string();
    }
    std::string dir(raw);
    if (dir.back() != '/') {
        dir.push_back('/');
    }
    return dir;
}

static long long env_ll(const char* name, long long fallback, long long lo, long long hi) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return fallback;
    }
    const long long value = std::atoll(raw);
    if (value < lo || value > hi) {
        return fallback;
    }
    return value;
}

// 单个日志文件的上限（字节）：默认 10 MiB。0 = 不轮转（旧行为：log.txt 一直增长）。
static size_t logMaxBytesFromEnv() {
    return static_cast<size_t>(env_ll("TETRIS_LOG_MAX_BYTES", 10LL * 1024 * 1024,
                                      0, 1024LL * 1024 * 1024));
}

// 轮转保留的**历史**份数（spdlog 的 max_files：当前 log.txt 另算）：
// 默认 5，即最多 6 个文件、总量 ≈ 60 MiB。
static size_t logMaxFilesFromEnv() {
    return static_cast<size_t>(env_ll("TETRIS_LOG_MAX_FILES", 5, 1, 100));
}

static std::shared_ptr<spdlog::logger> create_logger(bool per_process_file) {
    spdlog::init_thread_pool(65536, 2);
    spdlog::drop("logger");   // fork 后继承来的注册表里可能已有同名 logger

    const std::string log_dir = logDirectoryFromEnv();
    std::string file = log_dir + "log.txt";
    if (per_process_file) {
        file = log_dir + "log." + std::to_string(static_cast<long>(getpid())) + ".txt";
    }

    const size_t max_bytes = logMaxBytesFromEnv();
    std::shared_ptr<spdlog::logger> l;
    try {
        if (max_bytes == 0) {
            l = spdlog::basic_logger_mt<spdlog::async_factory>("logger", file);
        } else {
            // 写满 max_bytes 就改名为 log.1.txt（旧的依次后移，最老的删掉）
            l = spdlog::rotating_logger_mt<spdlog::async_factory>(
                    "logger", file, max_bytes, logMaxFilesFromEnv());
        }
    } catch (const spdlog::spdlog_ex& e) {
        // 目录不存在 / 没权限：退回 stderr。日志打不开是运维问题，
        // 不该把"游戏服务起不来"变成它的后果（systemd/journal 里能看到这句）。
        std::cerr << "❌ 无法打开日志文件 " << file << "：" << e.what()
                  << "（检查 TETRIS_LOG_DIR 是否存在、是否可写）" << std::endl;
        l = spdlog::stderr_color_mt("logger");
    }
    l->set_level(spdlog::level::warn);

    if (const char* env = std::getenv("TETRIS_LOG_LEVEL")) {
        auto level = spdlog::level::from_str(env);
        l->set_level(level);
        if (level != spdlog::level::warn) {
            l->flush_on(level);
        }
    }
    return l;
}

static int env_int(const char* name, int fallback, int lo, int hi) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return fallback;
    }
    int value = std::atoi(raw);
    if (value < lo || value > hi) {
        return fallback;
    }
    return value;
}

// 监听地址：默认 0.0.0.0（旧行为）。走 SSH 承载时设 127.0.0.1，让 9999 只对本机的
// tetris-bridge 可见 —— 公网入口只剩 sshd，明文协议没有机会被直接连上（docs/SSH_DEPLOY.md）。
static std::string bindAddressFromEnv() {
    const char* raw = std::getenv("TETRIS_BIND");
    return (raw != nullptr && *raw != '\0') ? std::string(raw) : std::string("0.0.0.0");
}

// 启动时把"实际生效"的配置打一遍：二十来个环境变量，配错了只从行为上很难查出来
// （比如 TETRIS_AUTH_IP_MAX_FAILURES 少写一个 0，表现是"偶尔莫名其妙锁人"）。
//
// 值都取自各模块自己的 configFromEnv()/访问器，**不在这里另抄一份默认值** ——
// 抄一份早晚会和模块漂移，那比不打印更糟（照着一个错的默认值去排查）。
static void printEffectiveConfig(int listen_port, int workers, int reuseport) {
    // 各模块的 configFromEnv() 在环境变量非法时会 logger->warn，所以先建一个只写
    // stderr 的 bootstrap logger；run_worker 会把它换成每进程的文件 logger。
    spdlog::drop("bootstrap");
    logger = spdlog::stderr_color_mt("bootstrap");
    logger->set_level(spdlog::level::warn);

    const AuthThrottle::Config auth = AuthThrottle::configFromEnv();
    const IpThrottle::Config ip = IpThrottle::configFromEnv();
    const int hashThreads = Filedata::hashThreadCount();
    const char* level = std::getenv("TETRIS_LOG_LEVEL");

    std::cout << "\n生效配置：" << std::endl;
    std::cout << "  监听地址    TETRIS_BIND             = " << bindAddressFromEnv() << std::endl;
    std::cout << "  监听端口    TETRIS_PORT             = " << listen_port << std::endl;
    std::cout << "  worker 数   TETRIS_WORKERS          = " << workers << std::endl;
    std::cout << "  REUSEPORT   TETRIS_REUSEPORT        = " << reuseport << std::endl;
    std::cout << "  日志级别    TETRIS_LOG_LEVEL        = "
              << (level != nullptr && *level != '\0' ? level : "warn（默认）") << std::endl;
    const std::string logDir = logDirectoryFromEnv();
    const size_t logMaxBytes = logMaxBytesFromEnv();
    std::cout << "  日志        TETRIS_LOG_*            = "
              << (logDir.empty() ? std::string("工作目录/") : logDir) << "log.txt，"
              << (logMaxBytes == 0
                      ? std::string("不轮转（log.txt 会一直增长）")
                      : std::to_string(logMaxBytes / 1024) + " KiB × "
                            + std::to_string(logMaxFilesFromEnv()) + " 份历史（+当前文件）")
              << std::endl;
    std::cout << "  数据库      TETRIS_DB               = " << Db::configuredPath() << std::endl;
    std::cout << "  哈希线程    TETRIS_HASH_THREADS     = " << hashThreads
              << (hashThreads <= 1 ? "（不开线程池，校验退回文件线程串行）" : "") << std::endl;
    std::cout << "  哈希队列    TETRIS_AUTH_QUEUE_LIMIT = "
              << Filedata::hashQueueLimit() << std::endl;
    std::cout << "  用户名退避  TETRIS_AUTH_*           = 前 " << auth.freeAttempts
              << " 次失败免延迟，之后 " << auth.baseDelayMs << "ms 起翻倍、"
              << auth.maxDelayMs << "ms 封顶；记录 " << auth.entryTtlMs / 1000
              << "s 过期，最多跟踪 " << auth.maxEntries << " 个用户名" << std::endl;
    std::cout << "  按 IP 兜底  TETRIS_AUTH_IP_*        = " << ip.maxFailures << " 次 / "
              << ip.windowMs / 1000 << "s 滑动窗口（" << IpThrottle::kSlots << " 槽，每槽 "
              << static_cast<double>(ip.windowMs) / IpThrottle::kSlots / 1000.0
              << "s）；最多跟踪 " << ip.maxEntries << " 个 IP" << std::endl;
    const RegThrottle::Config reg = RegThrottle::configFromEnv();
    std::cout << "  注册限流    TETRIS_REG_*           = 每 IP " << reg.ipMax << " 次 / "
              << reg.ipWindowMs / 1000 << "s，全局 " << reg.globalMax << " 次 / "
              << reg.globalWindowMs / 1000 << "s（0 = 关闭该维度）" << std::endl;
    std::cout << "  连接池      预创建 " << ConnectionPool::INITIAL_POOL_SIZE
              << " 个，上限 " << ConnectionPool::MAX_POOL_SIZE
              << " 个，空闲 trim 目标 " << ConnectionPool::trimTarget()
              << " 个（每 " << ConnectionPool::trimIntervalMs() << "ms 检查）" << std::endl;

    logger.reset();
    spdlog::drop("bootstrap");
}

// 一个 worker 的完整生命周期：日志 -> 连接池 -> 异步文件线程 -> 监听 socket -> 事件循环。
// 多 worker 时每个进程各跑一份，互不共享内存；共享的只有 SQLite 库文件（WAL + busy_timeout）。
static int run_worker(int listen_port, int workers, size_t pool_size, int reuseport) {
    logger = create_logger(workers > 1);

    // 系统优化必须在 logger 就绪之后做（SystemOptimizer 内部会写日志）。
    // fd 限制等是"每进程"的，所以每个 worker 各自做一遍才是对的。
    std::cout << "\n步骤0: 系统优化检查..." << std::endl;
    SystemOptimizer::optimizeForHighConcurrency();
    if (workers == 1) {
        SystemOptimizer::printCurrentLimits();
    }

    std::cout << "\n步骤1: 初始化连接池（本进程预创建 " << pool_size << " 个 User）..." << std::endl;
    ConnectionPool::initialize(pool_size);

    std::cout << "\n步骤2: 异步文件系统初始化..." << std::endl;
    if (!Filedata::initializeAsyncSystem()) {
        std::cerr << "❌ 异步文件系统初始化失败" << std::endl;
        return -1;
    }

    std::cout << "\n步骤3: 初始化网络服务..." << std::endl;
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        logger->error("create socket Error: {} (errno: {})", strerror(errno), errno);
        std::cerr << "❌ 创建socket失败" << std::endl;
        return -1;
    }

    // 设置socket选项
    int reuse = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        logger->error("setsockopt SO_REUSEADDR failed: {}", strerror(errno));
        std::cerr << "⚠️  设置SO_REUSEADDR失败" << std::endl;
    }

#ifdef SO_REUSEPORT
    if (reuseport) {
        // 多 worker 的核心：各进程独立 bind 同一端口，内核按连接做负载均衡
        int on = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0) {
            logger->error("setsockopt SO_REUSEPORT failed: {}", strerror(errno));
            std::cerr << "❌ 设置SO_REUSEPORT失败，无法多进程监听同一端口" << std::endl;
            close(serverSocket);
            return -1;
        }
    }
#endif

    // 设置发送和接收超时
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger->warn("setsockopt SO_SNDTIMEO failed: {}", strerror(errno));
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger->warn("setsockopt SO_RCVTIMEO failed: {}", strerror(errno));
    }

    // 绑定地址：默认 0.0.0.0（保持旧行为）。生产上走 SSH 承载时设
    // TETRIS_BIND=127.0.0.1，让 9999 只对本机的 tetris-bridge 可见 ——
    // 公网入口只剩 sshd，明文协议不再有被直接连上的机会。
    const std::string bind_addr = bindAddressFromEnv();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    if (inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1) {
        close(serverSocket);
        logger->error("TETRIS_BIND 不是合法的 IPv4 地址: {}", bind_addr);
        std::cerr << "❌ TETRIS_BIND 不是合法的 IPv4 地址: " << bind_addr << std::endl;
        return -1;
    }

    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(serverSocket);
        logger->error("bind Error: {} (errno: {})", strerror(errno), errno);
        std::cerr << "❌ 绑定失败（" << bind_addr << ":" << listen_port << "）" << std::endl;
        return -1;
    }

    // 监听
    if (listen(serverSocket, 65535) == -1) {  // 增大监听队列
        close(serverSocket);
        logger->error("listen Error: {} (errno: {})", strerror(errno), errno);
        std::cerr << "❌ 监听失败" << std::endl;
        return -1;
    }

    std::cout << "✅ 服务器初始化完成，监听 " << bind_addr << ":" << listen_port << std::endl;
    std::cout << "   监听队列大小: 65535" << std::endl;

    // 初始化libevent
    std::cout << "\n步骤4: 初始化事件循环..." << std::endl;
    EventLoop eventloop;

    // 注册服务器socket事件
    if (!eventloop.registerFdEvent(serverSocket, EV_READ | EV_PERSIST,
                                   Server::handleNewClientConnection, (void*)&eventloop)) {
        std::cerr << "❌ 注册服务器事件失败" << std::endl;
        return -1;
    }

    // 创建定时器事件
    struct timeval timer_delay = { 0, 100000 }; // 100ms
    if (!eventloop.registerFdEvent(-1, EV_PERSIST, Game::processTimerEvent, NULL, &timer_delay)) {
        std::cerr << "❌ 注册定时器事件失败" << std::endl;
        return -1;
    }

    // 创建清理事件
    if (!eventloop.registerFdEvent(-1, EV_PERSIST, EventLoop::unregister_Event_User, NULL, &timer_delay)) {
        std::cerr << "❌ 注册清理事件失败" << std::endl;
        return -1;
    }

    std::cout << "🎉 服务器启动完成!" << std::endl;
    std::cout << "==================================" << std::endl;
    printf("======waiting for client's request======\n");

    // 运行事件循环
    eventloop.run();

    // 清理异步文件系统
    Filedata::shutdownAsyncSystem();

    return 0;
}

// 父进程持有的子进程表（信号处理里要用，所以是全局的）
static pid_t g_children[256];
static int g_child_count = 0;

// Ctrl+C / kill 时把 SIGTERM 转发给所有 worker
static void forward_signal(int sig) {
    (void)sig;
    for (int i = 0; i < g_child_count; ++i) {
        if (g_children[i] > 0) {
            kill(g_children[i], SIGTERM);
        }
    }
}

// 单 worker 模式没有子进程可转发，但**必须**装一个处理器：
// 容器里默认的 1 号进程就是 tetris_game，而内核不会把"默认动作是终止"的信号
// 投递给 1 号进程 —— 不装的话 `docker stop` 会干等到超时再 SIGKILL，
// systemd 的 stop 也会拖到 TimeoutStopSec。
// 这里直接 _exit：SQLite 是 WAL + 事务，进程被硬杀也不会坏库；
// 不在信号处理器里做线程池/日志器的析构（那些操作不是 async-signal-safe 的）。
static void exit_on_signal(int) {
    _exit(0);
}

int main()
{
    std::cout << "🎮 俄罗斯方块服务器启动中..." << std::endl;
    std::cout << "==================================" << std::endl;

    int listen_port = env_int("TETRIS_PORT", DEFAULT_PORT, 1, 65535);
    int workers = env_int("TETRIS_WORKERS", 1, 1, (int)(sizeof(g_children) / sizeof(g_children[0])));

    // 是否给监听 socket 开 SO_REUSEPORT。默认跟随 worker 数，
    // 但允许强制打开/关闭 —— 压测时要能把"多进程"和"这个 socket 选项本身"
    // 拆开定位（单 worker + 开 reuseport 就能看出选项有没有代价）。
    int reuseport = env_int("TETRIS_REUSEPORT", (workers > 1) ? 1 : 0, 0, 1);

    // 打印生效配置要放在 fork 之前：每个 worker 都会建自己的 logger，
    // 放在里面会打印 N 遍（而配置本身在所有 worker 里是一样的）。
    printEffectiveConfig(listen_port, workers, reuseport);

    // 注意：系统优化放在 run_worker 里做（它要写日志，而 logger 是每进程创建的）

    if (workers == 1) {
        // 单 worker 也要能立刻响应 SIGTERM/SIGINT（容器里它是 1 号进程，见 exit_on_signal）
        signal(SIGINT, exit_on_signal);
        signal(SIGTERM, exit_on_signal);
        return run_worker(listen_port, 1, ConnectionPool::INITIAL_POOL_SIZE, reuseport);
    }

    // 多 worker：连接池按 worker 数均分，否则 N 个进程各预创建 5 万个 User，内存直接乘 N
    size_t pool_per_worker = ConnectionPool::INITIAL_POOL_SIZE / (size_t)workers;
    if (pool_per_worker < 1000) {
        pool_per_worker = 1000;
    }

    std::cout << "\n多进程模式：worker 数 = " << workers
              << "，端口 = " << listen_port
              << "，reuseport = " << reuseport
              << "，每进程预创建 User = " << pool_per_worker << std::endl;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, forward_signal);
    signal(SIGTERM, forward_signal);

    std::cout.flush();

    for (int i = 0; i < workers; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "❌ fork 失败（已启动 " << g_child_count << " 个 worker）" << std::endl;
            forward_signal(SIGTERM);
            break;
        }
        if (pid == 0) {
            // 子进程：信号恢复默认（父进程会转发 SIGTERM 进来）
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            int rc = run_worker(listen_port, workers, pool_per_worker, reuseport);
            // 用 _exit：不要重复刷新从父进程继承来的 stdio 缓冲
            _exit(rc == 0 ? 0 : 1);
        }
        g_children[g_child_count++] = pid;
        std::cout << "  worker " << (i + 1) << " pid = " << pid << std::endl;
    }

    // 回收所有 worker。收到 SIGINT/SIGTERM 时 forward_signal 会把 SIGTERM 发给它们，
    // 它们退出后 waitpid 返回，这个循环自然结束。
    int remaining = g_child_count;
    while (remaining > 0) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < g_child_count; ++i) {
            if (g_children[i] == pid) {
                g_children[i] = 0;
                --remaining;
                bool abnormal = !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
                std::cout << "  worker pid = " << pid << " 退出"
                          << (abnormal ? "（异常）" : "") << std::endl;
                break;
            }
        }
    }

    std::cout << "所有 worker 已退出" << std::endl;
    return 0;
}
