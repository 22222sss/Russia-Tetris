#pragma once
#ifndef Tetris_H
#define Tetris_H

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
#include <fcntl.h>

using namespace std;

#define MAXSIZE 2048
#define DEFAULT_PORT 9991// ָ���˿�Ϊ9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define WINDOW_ROW_COUNT 24 //��Ϸ������
#define WINDOW_COL_COUNT 20 //��Ϸ������

#define KEY_DOWN "\x1b[B" //���������
#define KEY_LEFT "\x1b[D" //���������
#define KEY_RIGHT "\x1b[C" //���������

using namespace std;

struct Block
{
    int space[4][4];
};

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��


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
    STATUS_PLAYING = 1,
    STATUS_OVER_CONFIRMING = 0,
    STATUS_OVER_QUIT = -1
};


bool IsSetSocketBlocking(int socket, bool blocking);

//��ɫ����
int Color(int c);

//��ʼ��������Ϣ
void InitBlockInfo();

#endif