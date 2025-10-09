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

    // ʹ�� using ���庯��ָ������
    using EventCallback = void (*)(evutil_socket_t, short, void*);

    // ����ʹ�� typedef ���庯��ָ������
    //typedef void (*EventCallback)(evutil_socket_t, short, void*);

    static map<evutil_socket_t, struct event*> events;//�洢��ע����¼�

public:
    EventLoop();
    ~EventLoop();

    // �����¼�ѭ��
    void run();

    // ע���ļ��������¼�
    bool registerFdEvent(evutil_socket_t fd, short events, EventCallback callback, void* arg, const struct timeval* timeout = nullptr);

    // ע���¼��������û�
    static void unregister_Event_User(int timerfd, short events, void* arg);

    // Getter and Setter for events
    static map<evutil_socket_t, struct event*> getEvents();

    static void setEvents(const map<evutil_socket_t, struct event*>& newEvents);
;
};

#endif // EventLoop_H