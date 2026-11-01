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

// 输出批量：把"处理一条连接的一次事件"里的多次 output() 合并成一次 send。
//
// 一次按键的重绘会调用十几到二十次 outputText（每个方块格子一次），原来每次
// outputText 都直接 send，等于一次按键几十个 syscall。用 RAII 在事件处理外面
// 包一层，所有输出先入 User 的发送缓冲，析构时统一 flush。
//
// 只在事件循环线程使用；嵌套构造会累加深度，最外层析构才真正发送。
class OutputBatch {
public:
    explicit OutputBatch(const std::shared_ptr<User>& user);
    ~OutputBatch();

    OutputBatch(const OutputBatch&) = delete;
    OutputBatch& operator=(const OutputBatch&) = delete;

private:
    std::shared_ptr<User> user_;
};

bool moveTo(const shared_ptr<User>& user, int row, int col);

bool ChangeCurrentColor(const shared_ptr<User>& user, int n);

bool outputText(const shared_ptr<User>& user, int row, int col, int color, const string& text, int grade = 0);

bool isNumber(const std::string& s);// 检查字符串是否为数字

#endif
