#pragma once
#ifndef Utility_H
#define Utility_H

#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

//设置非阻塞/阻塞套接字
bool IsSetSocketBlocking(int socket, bool blocking);

//颜色设置
int Color(int c);

//初始化方块信息
void InitBlockInfo();

// 检查用户是否存在
bool isUserExists(const std::string& username);

//获取目前时间
string currenttime();

bool output(User* user, string s);

bool moveTo(User* user, int row, int col);

bool ChangeCurrentColor(User* user, int n);

bool outputText(User* user, int row, int col, int n, string s);

bool outputgrade(User* user, int row, int col, int n, string s, int grade);



#endif
