#pragma once
#ifndef Utility_H
#define Utility_H

#include"../Common/Common.h"
#include"../TetrisGame/User.h"
#include"../TetrisGame/Game.h"
#include"../UImanage/UImanage.h"
#include"../TetrisGame/Filedata.h"
#include"../EventLoop/EventLoop.h"
#include"../TetrisGame/PlayerInfo.h"

//设置非阻塞/阻塞套接字
bool IsSetSocketBlocking(int socket, bool blocking);

//颜色设置
int Color(int c);

// 检查用户是否存在
bool isUserExists(const std::string& username);

//获取目前时间
string currenttime();

bool output(const shared_ptr<User>& user, string s);

// 尽量把该用户缓冲里的待发数据写出去；遇到 EAGAIN 就把剩余部分留在缓冲区，
// 由 100ms 定时器下次再试（见 EventLoop::unregister_Event_User）
bool flushUserOutput(const shared_ptr<User>& user);

bool moveTo(const shared_ptr<User>& user, int row, int col);

bool ChangeCurrentColor(const shared_ptr<User>& user, int n);

bool outputText(const shared_ptr<User>& user, int row, int col, int color, const string& text, int grade = 0);

bool isNumber(const std::string& s);// 检查字符串是否为数字

#endif
