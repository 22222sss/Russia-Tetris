#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

vector<PlayerInfo*> players = {};

map<int, User*> users = {};

Block blockDefines[7][4] = { 0 };

map<evutil_socket_t, struct event*> event = {};

// 创建 spdlog::logger 对象
std::shared_ptr<spdlog::logger> logger = spdlog::basic_logger_mt("logger", "log.txt");

int main()
{
    signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        //printf("create socket Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("create socket Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }
    // 对应伪代码中的bind(sockfd, ip::port和一些配置);
    struct sockaddr_in addr;    // 用于存放ip和端口的结构
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEFAULT_PORT);
    if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        close(serverSocket);
        //printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("bind Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }
    // 对应伪代码中的listen(sockfd);    
    if (listen(serverSocket, MAXLINK) == -1)
    {
        close(serverSocket);
        //printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("listen Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        return -1;
    }

    // 初始化 libevent
    struct event_base* base = event_base_new();
    if (!base) {
        logger->error("Error initializing libevent: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();

        return 1;
    }



    //创建并添加服务器套接字事件
    struct event* serverEvent = event_new(base, serverSocket, EV_READ | EV_PERSIST, handleNewClientConnection, (void*)base);

    if (!serverEvent)
    {
        logger->error("Error creating serverEvent.");
        logger->flush();
        return 1;
    }

    event_add(serverEvent, NULL);


    // 创建定时器事件
    struct event* timerEvent = event_new(base, -1, EV_PERSIST, processTimerEvent, NULL);
    if (!timerEvent) {
        logger->error("Error creating timer event.");
        logger->flush();
        return 1;
    }

    // 设置定时器的超时时间为 0.1 秒
    struct timeval delay = { 0, 100000 }; // 100000 微秒 = 0.1 秒

    event_add(timerEvent, &delay);


    printf("======waiting for client's request======\n");

    event_base_dispatch(base);

    return 0;
}