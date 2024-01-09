#include"Player.h"
#include"Public.h"
#include"Public_game.h"
#include"UserInfo.h"
#include"Game.h"
#include"EventLoop.h"
#include"UserInfo.h"

vector<Player*> players = {};

map<int, UserInfo*> g_playing_gamer = {};

Block blockDefines[7][4] = { 0 };

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

    int epollfd = epoll_create(MAXSIZE);
    if (epollfd < 0)	//创建epoll实例
    {
        close(serverSocket);
        //printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("epoll_create Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        exit(-1);
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = serverSocket;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &event) == -1)
    {
        //printf("add server socket to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("add server socket to epoll Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        close(serverSocket);
        close(epollfd);
        return -1;
    }

    // 创建定时器
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec timer_spec;
    timer_spec.it_interval.tv_sec = 0;  // 避免重复触发
    timer_spec.it_interval.tv_nsec = 0;
    timer_spec.it_value.tv_sec = 1;     // 初始延时一秒
    timer_spec.it_value.tv_nsec = 0;
    timerfd_settime(timerfd, 0, &timer_spec, NULL);


    // 将定时器加入 epoll 监听集合
    struct epoll_event timer_event;
    timer_event.events = EPOLLIN;
    timer_event.data.fd = timerfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, timerfd, &timer_event) == -1)
    {
        //printf("add timerfd to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
        logger->error("add timerfd to epoll Error: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        close(serverSocket);
        close(epollfd);
        return 1;
    }

    printf("======waiting for client's request======\n");

    EventLoop eventloop(serverSocket, timerfd);

    Server* server = new Server;

    Game* game = new Game;

    eventloop.Run(server, game, epollfd);


	return 0;
}