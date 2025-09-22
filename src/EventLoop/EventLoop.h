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



void handleNewClientConnection(int serverSocket, short events, void* arg);

void handleClientData(int clientSocket, short events, void* arg);

// 定义处理方块下降逻辑的函数
void processBlockDown(User* user);

//处理定时触发逻辑
void handleTimedUserLogic(User* user);

//没有键盘输入时，方块自动下降
void processTimerEvent(int timerfd, short events, void* arg);



#endif // EventLoop_H

