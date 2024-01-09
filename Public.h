#pragma once
#ifndef Public_H
#define Public_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <signal.h>
#include <vector>
#include <map>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <algorithm>
#include <queue>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

using namespace std;

#define MAXSIZE 2048
#define DEFAULT_PORT 9997// 指定端口为9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define WINDOW_ROW_COUNT 24 //游戏区行数
#define WINDOW_COL_COUNT 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

struct Block
{
    int space[4][4];
};

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern shared_ptr<spdlog::logger> logger;


enum Shape
{
    SHAPE_T = 0,
    SHAPE_L = 1,
    SHAPE_J = 2,
    SHAPE_Z = 3,
    SHAPE_S = 4,
    SHAPE_O = 5,
    SHAPE_I = 6
};

enum Color
{
    COLOR_PURPLE = 35,
    COLOR_RED = 31,
    COLOR_LOWBLUE = 36,
    COLOR_YELLO = 33,
    COLOR_DEEPBLUE = 34,
    COLOR_WHITE = 37
};

enum GameStatus
{
    STATUS_NOTSTART ,
    STATUS_PLAYING ,
    STATUS_OVER_CONFIRMING ,
    STATUS_OVER_QUIT,

    STATUS_LOGIN,
    STATUS_LOGIN_OVER,

    STATUS_RECEIVE_USERNAME_REGISTER,
    STATUS_RECEIVE_PASSWORD_REGISTER,

    STATUS_RECEIVE_USERNAME_LOAD,
    STATUS_RECEIVE_PASSWORD_LOAD,

    STATUS_REGISTER_OR_LOAD_OVER,

    STATUS_SELECT_GAME_DIFFICULTY
};

//设置非阻塞/阻塞套接字
bool IsSetSocketBlocking(int socket, bool blocking);

//颜色设置
int Color(int c);

//初始化方块信息
void InitBlockInfo();

// 检查用户是否存在
bool isUserExists(const std::string& username);

// 读取用户数据文件，初始化users向量
bool loadPlayerData();

//获取目前时间
string currenttime();

#endif
