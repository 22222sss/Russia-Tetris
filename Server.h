#pragma once
#ifndef Server_H
#define Server_H

#include"UserInfo.h"

class Server {
public:
    Server(int serverSocket, int epollfd, int timerfd);

    void handleNewClientConnection();

    // 定义处理用户逻辑的函数
    void processUserLogic(UserInfo* user);

    void processTimerEvent();

    void handleClientData(UserInfo* userInfo);

    void processEvents(int readyCount, epoll_event* events);

    void Run();

private:
    int serverSocket;
    int epollfd;
    int timerfd;
};

#endif // !Server_H

