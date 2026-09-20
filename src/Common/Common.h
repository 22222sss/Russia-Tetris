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

#include <sys/stat.h>
#include <algorithm>

#include <unordered_map>  // 替换map为unordered_map
#include <memory>
#include <mutex>          // 添加线程安全
#include <vector>         // 用于批量操作
#include <atomic>  // 用于线程安全的计数器
#include <condition_variable>

#include <iconv.h>
#include <errno.h>


using namespace std;

#define MAXSIZE 2048
#define DEFAULT_PORT 9999// 指定端口为9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define WINDOW_ROW_COUNT 24 //游戏区行数
#define WINDOW_COL_COUNT 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

enum GameStatus
{
    STATUS_NOTSTART,
    STATUS_PLAYING,
    STATUS_OVER_CONFIRMING,
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
