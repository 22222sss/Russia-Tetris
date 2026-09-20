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

// UTF-8 转 GBK 函数
string utf8_to_gbk(const std::string& utf8_str);

//设置非阻塞/阻塞套接字
bool IsSetSocketBlocking(int socket, bool blocking);

//颜色设置
int Color(int c);

// 检查用户是否存在
bool isUserExists(const std::string& username);

//获取目前时间
string currenttime();

bool output(const shared_ptr<User>& user, string s);

bool moveTo(const shared_ptr<User>& user, int row, int col);

bool ChangeCurrentColor(const shared_ptr<User>& user, int n);

bool outputText(const shared_ptr<User>& user, int row, int col, int color, const string& text, int grade = 0);

bool isNumber(const std::string& s);// 检查字符串是否为数字

#endif
