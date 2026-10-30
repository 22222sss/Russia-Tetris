#include"../Common/Common.h"
#include"../TetrisGame/User.h"
#include"../TetrisGame/Game.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../TetrisGame/Server.h"
#include"../EventLoop/EventLoop.h"
#include"../TetrisGame/Filedata.h"
#include"../TetrisGame/PlayerInfo.h"
#include"../Utility/SystemOptimizer.h"
#include <spdlog/async.h>
#include <sys/wait.h>

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
static std::shared_ptr<spdlog::logger> create_logger(bool per_process_file) {
    spdlog::init_thread_pool(65536, 2);
    spdlog::drop("logger");   // fork 后继承来的注册表里可能已有同名 logger

    std::string file = "log.txt";
    if (per_process_file) {
        file = "log." + std::to_string(static_cast<long>(getpid())) + ".txt";
    }

    auto l = spdlog::basic_logger_mt<spdlog::async_factory>("logger", file);
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

// 一个 worker 的完整生命周期：日志 -> 连接池 -> 异步文件线程 -> 监听 socket -> 事件循环。
// 多 worker 时每个进程各跑一份，互不共享内存；共享的只有 userdata.csv（靠 flock 保护）。
static int run_worker(int listen_port, int workers, size_t pool_size) {
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
    if (workers > 1) {
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

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port);

    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(serverSocket);
        logger->error("bind Error: {} (errno: {})", strerror(errno), errno);
        std::cerr << "❌ 绑定端口失败（端口 " << listen_port << "）" << std::endl;
        return -1;
    }

    // 监听
    if (listen(serverSocket, 65535) == -1) {  // 增大监听队列
        close(serverSocket);
        logger->error("listen Error: {} (errno: {})", strerror(errno), errno);
        std::cerr << "❌ 监听失败" << std::endl;
        return -1;
    }

    std::cout << "✅ 服务器初始化完成，监听端口: " << listen_port << std::endl;
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

int main()
{
    std::cout << "🎮 俄罗斯方块服务器启动中..." << std::endl;
    std::cout << "==================================" << std::endl;

    int listen_port = env_int("TETRIS_PORT", DEFAULT_PORT, 1, 65535);
    int workers = env_int("TETRIS_WORKERS", 1, 1, (int)(sizeof(g_children) / sizeof(g_children[0])));

    // 注意：系统优化放在 run_worker 里做（它要写日志，而 logger 是每进程创建的）

    if (workers == 1) {
        return run_worker(listen_port, 1, ConnectionPool::INITIAL_POOL_SIZE);
    }

    // 多 worker：连接池按 worker 数均分，否则 N 个进程各预创建 5 万个 User，内存直接乘 N
    size_t pool_per_worker = ConnectionPool::INITIAL_POOL_SIZE / (size_t)workers;
    if (pool_per_worker < 1000) {
        pool_per_worker = 1000;
    }

    std::cout << "\n多进程模式：worker 数 = " << workers
              << "，端口 = " << listen_port
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
            int rc = run_worker(listen_port, workers, pool_per_worker);
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
