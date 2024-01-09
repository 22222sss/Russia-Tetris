#include"Game.h"
#include"Public.h"
#include"UserInfo.h"
#include"Public_game.h"
#include"Player.h"
#include"EventLoop.h"
#include"Server.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, UserInfo*> g_playing_gamer;

extern shared_ptr<spdlog::logger> logger;

EventLoop::EventLoop(int serverSocket, int timerfd) : serverSocket(serverSocket), timerfd(timerfd) {}

void EventLoop::handleNewClientConnection(Server *server,int epollfd)
{
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1)
    {
        //printf("accept Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        logger->error("accept Error: {} (errno: {}) In handleNewClientConnection\n", strerror(errno), errno);
        logger->flush();
        return;
    }
    else
    {
        //printf("Client[%d], welcome!\n", clientSocket);
        //Client.push_back(client);
        logger->info("Client[{}], welcome!\n", clientSocket);
        logger->flush();
    }

    // �����µ��û���Ϣ�ṹ��
    UserInfo* newUser = new UserInfo(clientSocket);

    if (newUser == nullptr)
    {
        close(clientSocket);
        //printf("allocate memory for newUser Error In handleNewClientConnection");
        logger->error("allocate memory for newUser Error In handleNewClientConnection\n");
        logger->flush();
        return;
    }

    // ���ÿͻ�������Ϊ������ģʽ
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    // �������ӵ��¼���ӵ� epoll ʵ����

    struct epoll_event newEvent;
    newEvent.events = EPOLLIN | EPOLLET; // �������¼�����EPOLL��Ϊ��Ե����(Edge Triggered)ģʽ��
    newEvent.data.ptr = newUser; // ��ָ��ָ���û���Ϣ�ṹ��

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
    {
        //printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        logger->error("add new client event to epoll instance Error: {} (errno: {}) In handleNewClientConnection\n", strerror(errno), errno);
        logger->flush();
        close(clientSocket);
        delete newUser;
        return;
    }

    if (!server->showInitMenu(newUser))
        return;
}

void EventLoop::handleClientData(Server *server,Game *game,UserInfo* user, int epollfd)
{
    // ���������ӿͻ��˵����ݽ����¼�
    if (user->status == STATUS_PLAYING)
    {
        if (!game->process_STATUS_PLAYING(user))
        {
            return;
        }
    }
    else if (user->status == STATUS_OVER_CONFIRMING)
    {
        if (!game->process_STATUS_OVER_CONFIRMING(server,user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_NOTSTART)
    {
        if (!server->process_STATUS_NOTSTART(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_RECEIVE_USERNAME_REGISTER)
    {
        if (!server->process_STATUS_RECEIVE_USERNAME_REGISTER(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_RECEIVE_PASSWORD_REGISTER)
    {
        if (!server->process_STATUS_RECEIVE_PASSWORD_REGISTER(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_RECEIVE_USERNAME_LOAD)
    {
        if (!server->process_STATUS_RECEIVE_USERNAME_LOAD(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_RECEIVE_PASSWORD_LOAD)
    {
        if (!server->process_STATUS_RECEIVE_PASSWORD_LOAD(user,epollfd))
        {
            return;
        }

    }
    else if (user->status == STATUS_LOGIN)
    {
        if (!server->process_STATUS_LOGIN(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_SELECT_GAME_DIFFICULTY)
    {
        if (!game->process_STATUS_SELECT_GAME_DIFFICULTY(server,user,epollfd))
        {
            return;
        }

    }
    else if (user->status == STATUS_LOGIN_OVER)
    {
        if (!server->process_STATUS_LOGIN_OVER(user,epollfd))
        {
            return;
        }
    }
    else if (user->status == STATUS_REGISTER_OR_LOAD_OVER)
    {
        if (!server->process_STATUS_REGISTER_OR_LOAD_OVER(user,epollfd))
        {
            return;
        }
    }
}



void EventLoop::processEvents(Server* server, Game* game, int readyCount, epoll_event* events, int epollfd)
{
    for (int i = 0; i < readyCount; ++i)
    {
        UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;

        if (currentFd == serverSocket)
        {
            this->handleNewClientConnection(server,epollfd);
        }
        else if (currentFd == timerfd)
        {
            if (!g_playing_gamer.empty())
            {
                game->processTimerEvent(epollfd);
            }
        }
        else
        {
            this->handleClientData(server,game,userInfo, epollfd);
        }
    }

}

void EventLoop::Run(Server* server, Game* game,int epollfd)
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) {
            //printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
            logger->error("Failed on epoll_wait: {} (errno: {})\n", strerror(errno), errno);
            logger->flush();
            continue;
        }
        processEvents(server, game, readyCount, events, epollfd);
    }
}