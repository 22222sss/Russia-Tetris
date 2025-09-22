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

// ���崦�����½��߼��ĺ���
void processBlockDown(User* user);

//����ʱ�����߼�
void handleTimedUserLogic(User* user);

//û�м�������ʱ�������Զ��½�
void processTimerEvent(int timerfd, short events, void* arg);



#endif // EventLoop_H

