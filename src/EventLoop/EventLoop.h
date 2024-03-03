#pragma once
#ifndef EventLoop_H
#define EventLoop_H


#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

class EventLoop {
public:
    EventLoop(int serverSocket, int timerfd);

    void handleNewClientConnection(int epollfd);

    void handleClientData(User* user);

    void processBlockDown(User* user);

    void handleTimedUserLogic(User* user);

    void processTimerEvent();

    void processEvents(int readyCount, epoll_event* events, int epollfd);

    void Run(int epollfd);

private:
    int serverSocket;
    int timerfd;
};

#endif // EventLoop_H

