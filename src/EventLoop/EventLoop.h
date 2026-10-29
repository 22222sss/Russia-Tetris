#pragma once
#ifndef EventLoop_H
#define EventLoop_H

#include"../Common/Common.h"
#include"../TetrisGame/User.h"
#include"../TetrisGame/Game.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../TetrisGame/Filedata.h"
#include"../TetrisGame/PlayerInfo.h"
#include"../ConnectionPool/ConnectionPool.h"

class EventLoop {
private:
    struct event_base* base;

    using EventCallback = void (*)(evutil_socket_t, short, void*);

    // 使用unordered_map提高查找性能 - O(1) vs O(log n)
    static std::unordered_map<evutil_socket_t, struct event*> events;
    static std::mutex events_mutex;  // 保护并发访问

    // 自管道的读端回调：把排队的任务在事件循环线程上执行
    static void onPost(evutil_socket_t fd, short events, void* arg);

public:
    EventLoop();
    ~EventLoop();

    void run();

    // 把任务投递到事件循环线程执行。
    // 异步文件线程的回调不能直接操作 User / socket（会和事件循环线程竞争
    // receivedata、status 等字段），必须经由这里回到循环线程再执行。
    static void post(std::function<void()> task);

    // 添加批量事件注册功能 - 提高初始化性能
    bool registerFdEvent(evutil_socket_t fd, short events, EventCallback callback, void* arg, const struct timeval* timeout = nullptr);
    bool batchRegisterEvents(const std::vector<evutil_socket_t>& fds, short events, EventCallback callback, void* arg);

    // 释放某个 fd 的事件对象（连接结束时必须调用）
    // 不调用会导致：事件对象泄漏，且该 fd 被内核复用时 libevent 不会重新
    // epoll_ctl(ADD)，新连接将永远收不到可读事件（即"连接静默"）
    static void removeFdEvent(evutil_socket_t fd);

    static void unregister_Event_User(int timerfd, short events, void* arg);

    // 添加事件统计功能 - 便于监控和调试
    static size_t getActiveEventsCount();
    static void printEventStatistics();

    static std::unordered_map<evutil_socket_t, struct event*> getEvents();
    static void setEvents(const std::unordered_map<evutil_socket_t, struct event*>& newEvents);
};

#endif // EventLoop_H