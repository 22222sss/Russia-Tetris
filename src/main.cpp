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

// 创建 spdlog::logger 对象（异步日志，避免高并发下同步写盘阻塞主线程）
// 通过初始化函数配置：大队列 + 2 个写线程，避免队列溢出阻塞；生产环境只记录 warn 及以上
static std::shared_ptr<spdlog::logger> create_logger() {
    spdlog::init_thread_pool(65536, 2);
    auto l = spdlog::basic_logger_mt<spdlog::async_factory>("logger", "log.txt");
    l->set_level(spdlog::level::warn);
    return l;
}
std::shared_ptr<spdlog::logger> logger = create_logger();

int main()
{
    std::cout << "🎮 俄罗斯方块服务器启动中..." << std::endl;
    std::cout << "==================================" << std::endl;

    // 步骤1: 系统优化检查
    std::cout << "\n步骤1: 系统优化检查..." << std::endl;
    SystemOptimizer::optimizeForHighConcurrency();
    SystemOptimizer::printCurrentLimits();

    // 步骤2: 初始化连接池
    std::cout << "\n步骤2: 初始化连接池..." << std::endl;
    ConnectionPool::initialize();

    // 🆕 新增：启动异步文件系统
    std::cout << "\n步骤3: 异步文件系统初始化..." << std::endl;
    if (!Filedata::initializeAsyncSystem()) {
        std::cerr << "❌ 异步文件系统初始化失败" << std::endl;
        return -1;
    }

    // 步骤3: 网络服务初始化
    std::cout << "\n步骤3: 初始化网络服务..." << std::endl;
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        logger->error("create socket Error: {} (errno: {})\n", strerror(errno), errno);
        std::cerr << "❌ 创建socket失败" << std::endl;
        return -1;
    }

    // 设置socket选项
    int reuse = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        logger->error("setsockopt SO_REUSEADDR failed: {}\n", strerror(errno));
        std::cerr << "⚠️  设置SO_REUSEADDR失败" << std::endl;
    }

    // 设置发送和接收超时
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger->warn("setsockopt SO_SNDTIMEO failed: {}\n", strerror(errno));
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger->warn("setsockopt SO_RCVTIMEO failed: {}\n", strerror(errno));
    }

    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEFAULT_PORT);

    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(serverSocket);
        logger->error("bind Error: {} (errno: {})\n", strerror(errno), errno);
        std::cerr << "❌ 绑定端口失败" << std::endl;
        return -1;
    }

    // 监听
    if (listen(serverSocket, 65535) == -1) {  // 增大监听队列
        close(serverSocket);
        logger->error("listen Error: {} (errno: {})\n", strerror(errno), errno);
        std::cerr << "❌ 监听失败" << std::endl;
        return -1;
    }

    std::cout << "✅ 服务器初始化完成，监听端口: " << DEFAULT_PORT << std::endl;
    std::cout << "   监听队列大小: 65535" << std::endl;

    // 初始化libevent
    std::cout << "\n步骤3: 初始化事件循环..." << std::endl;
    EventLoop eventloop;

    // 注册服务器socket事件
    if (!eventloop.registerFdEvent(serverSocket, EV_READ | EV_PERSIST, Server::handleNewClientConnection, (void*)&eventloop)) {
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