#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern shared_ptr<spdlog::logger> logger;



EventLoop::EventLoop() 
{
    base = event_base_new();
    if (!base) {
        logger->error("Error initializing libevent: {} (errno: {})\n", strerror(errno), errno);
        logger->flush();
        // �����׳��쳣�����ñ�־λ�������ܷ�����ֵ
        throw std::runtime_error("Failed to create event base");
    }
}

EventLoop::~EventLoop() {
    if (base) {
        event_base_free(base);
    }
}

void EventLoop::run() {
    if (base) {
        event_base_dispatch(base);
    }
}

bool EventLoop::registerFdEvent(evutil_socket_t fd, short events, EventCallback callback, void* arg, const struct timeval* timeout)
{
    struct event* Event = event_new(base, fd, events, callback, (void*)arg);

    if (!Event)
    {
        logger->error("Error creating Event.");
        logger->flush();
        return false;
    }

    EventLoop::getEvents().insert(make_pair(fd, Event));

    event_add(Event, timeout);

    return true;
}

void EventLoop::unregister_Event_User(int timerfd, short events, void* arg)
{
    if (!User::getUsers().empty())
    {
        for (auto i = User::getUsers().begin(); i != User::getUsers().end();)
        {
            // ִ��һЩ�������
            if (i->second->getStatus() == STATUS_OVER_QUIT)
            {
                auto eraseIter = i++;
                //printf("Client[%d] disconnected!\n", eraseIter->second->fd);
                logger->info("Client[{}] disconnected!\n", eraseIter->second->getFd());
                logger->flush();

                // ɾ���ض��ļ���������Ӧ���¼�
                auto it1 = EventLoop::getEvents().find(eraseIter->second->getFd());

                if (it1 != EventLoop::getEvents().end())
                {
                    struct event* ev_to_delete = it1->second;
                    event_del(ev_to_delete);
                    event_free(ev_to_delete);
                    EventLoop::getEvents().erase(it1); // �ӹ�ϣ����ɾ����Ӧ��ӳ��
                }

                close(eraseIter->second->getFd());

                auto it = User::getUsers().find(eraseIter->second->getFd());
                if (it != User::getUsers().end())
                {
                    User::getUsers().erase(eraseIter); // �� map ��ɾ��Ԫ��
                }
                delete eraseIter->second;
            }
            else
            {
                i++;
            }
        }
    }
}


map<evutil_socket_t, struct event*> EventLoop::getEvents()
{
    // ����˽�о�̬��Ա����
    static map<evutil_socket_t, struct event*> events;

    return events;
}

void EventLoop::setEvents(const map<evutil_socket_t, struct event*>& newEvents)
{

}





