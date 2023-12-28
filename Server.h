#pragma once
#ifndef Server_H
#define Server_H

#include"UserInfo.h"

class Server {
public:
    Server(int serverSocket, int timerfd);

    void handleNewClientConnection(int epollfd);

    void processTimerEvent(int eploofd);

    void handleClientData(UserInfo* userInfo,int epollfd);

    void processEvents(int readyCount, epoll_event* events, int epollfd);

    void Run(int epollfd);

private:
    int serverSocket;
    int timerfd;
};

#endif // !Server_H

