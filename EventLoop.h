#pragma once
#ifndef EventLoop_H
#define EventLoop_H

#include"UserInfo.h"
#include"Server.h"
#include"Game.h"

class EventLoop {
public:
    EventLoop(int serverSocket, int timerfd);

    void handleNewClientConnection(Server* server, int epollfd);

    void handleClientData(Server* server, Game* game, UserInfo* user, int epollfd);

    void processEvents(Server* server, Game* game, int readyCount, epoll_event* events, int epollfd);

    void Run(Server* server, Game* game, int epollfd);

private:
    int serverSocket;
    int timerfd;
};

#endif // EventLoop_H

