#pragma once
#ifndef Common_H
#define Common_H

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

#include <event2/event.h>  
#include <event2/bufferevent.h>  
#include <event2/listener.h>  
#include <event2/bufferevent.h>  
#include <event2/buffer.h>  
#include <event2/util.h>
#include <event2/thread.h>

#include <event.h>
#include <time.h>


using namespace std;

#define MAXSIZE 2048
#define DEFAULT_PORT 9999// ָ���˿�Ϊ9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define WINDOW_ROW_COUNT 24 //��Ϸ������
#define WINDOW_COL_COUNT 20 //��Ϸ������

#define KEY_DOWN "\x1b[B" //���������
#define KEY_LEFT "\x1b[D" //���������
#define KEY_RIGHT "\x1b[C" //���������

struct Block
{
    int space[4][4];
};

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

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


#endif
