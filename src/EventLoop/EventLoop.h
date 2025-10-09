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

class EventLoop {

private:
    struct event_base* base;

    // 使用 using 定义函数指针类型
    using EventCallback = void (*)(evutil_socket_t, short, void*);

    // 或者使用 typedef 定义函数指针类型
    //typedef void (*EventCallback)(evutil_socket_t, short, void*);

    static map<evutil_socket_t, struct event*> events;//存储已注册的事件

public:
    EventLoop();
    ~EventLoop();

    // 启动事件循环
    void run();

    // 注册文件描述符事件
    bool registerFdEvent(evutil_socket_t fd, short events, EventCallback callback, void* arg, const struct timeval* timeout = nullptr);

    // 注销事件和下线用户
    static void unregister_Event_User(int timerfd, short events, void* arg);

    // Getter and Setter for events
    static map<evutil_socket_t, struct event*> getEvents();

    static void setEvents(const map<evutil_socket_t, struct event*>& newEvents);
;
};

#endif // EventLoop_H