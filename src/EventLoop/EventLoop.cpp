#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

extern vector<PlayerInfo*> players;

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, User*> users;

extern shared_ptr<spdlog::logger> logger;

EventLoop::EventLoop(int serverSocket, int timerfd) : serverSocket(serverSocket), timerfd(timerfd) {}

void EventLoop::handleNewClientConnection(int epollfd)
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
    User* newUser = new User(clientSocket);

    if (newUser == nullptr)
    {
        close(clientSocket);
        //printf("allocate memory for newUser Error In handleNewClientConnection");
        logger->error("allocate memory for newUser Error In handleNewClientConnection\n");
        logger->flush();
        return;
    }

    users.insert(make_pair(clientSocket, newUser));

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

    std::unique_ptr<UImanage> UI(new UImanage);

    if (!UI->showInitMenu(newUser))
        return;
}

void EventLoop::handleClientData(User* user)
{
    //std::unique_ptr<Server> server(new Server);

    //std::unique_ptr<TetrisGame> game(new TetrisGame);

    Server* server = new Server;

    TetrisGame* game = new TetrisGame;

    // ���������ӿͻ��˵����ݽ����¼�
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!game->process_STATUS_PLAYING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_OVER_CONFIRMING)
    {
        if (!game->process_STATUS_OVER_CONFIRMING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_NOTSTART)
    {
        if (!server->process_STATUS_NOTSTART(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_USERNAME_REGISTER)
    {
        if (!server->process_STATUS_RECEIVE_USERNAME_REGISTER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_PASSWORD_REGISTER)
    {
        if (!server->process_STATUS_RECEIVE_PASSWORD_REGISTER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_USERNAME_LOAD)
    {
        if (!server->process_STATUS_RECEIVE_USERNAME_LOAD(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_PASSWORD_LOAD)
    {
        if (!server->process_STATUS_RECEIVE_PASSWORD_LOAD(user))
        {
            return;
        }

    }
    else if (user->getStatus() == STATUS_LOGIN)
    {
        if (!server->process_STATUS_LOGIN(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_SELECT_GAME_DIFFICULTY)
    {
        if (!game->process_STATUS_SELECT_GAME_DIFFICULTY(user))
        {
            return;
        }

    }
    else if (user->getStatus() == STATUS_LOGIN_OVER)
    {
        if (!server->process_STATUS_LOGIN_OVER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_REGISTER_OR_LOAD_OVER)
    {
        if (!server->process_STATUS_REGISTER_OR_LOAD_OVER(user))
        {
            return;
        }
    }
}

void EventLoop::processBlockDown(User* user)
{
    std::unique_ptr<TetrisGame> game(new TetrisGame);

    std::unique_ptr<UImanage> UI(new UImanage);

    std::unique_ptr<Filedata> filedata(new Filedata);

    if (game->IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (blockDefines[user->getShape()][user->getForm()].space[i][j] == 1)
                {
                    user->setData(user->getRow() + i - 1, user->getCol() + j - 1, 1);

                    user->setColor(user->getRow() + i - 1, user->getCol() + j - 1, user->getShape());
                }
            }
        }

        //user->line = 0;

        user->setLine(0);

        while (1)
        {
            if (game->Is_Increase_Score(user) == 1)
            {
                continue;
            }
            else if (game->Is_Increase_Score(user) == 0)
            {
                break;
            }
            else if (game->Is_Increase_Score(user) == -1)
            {
                return;
            }
        }

        if (!game->UpdateCurrentScore(user))
        {
            return;
        }

        if (!game->IsOver(user))//�ж��Ƿ����
        {
            user->setShape(user->getNextShape());
            user->setForm(user->getNextForm());

            if (!UI->DrawSpace(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->setNextShape(rand() % 7);
            user->setNextForm(rand() % 4);

            user->setRow(1);
            user->setCol(WINDOW_COL_COUNT / 2 - 1);

            if (!UI->DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
            {
                return;
            }

            if (!UI->DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//���÷�����ʾ�ڳ�ʼ����λ��
            {
                return;
            }
        }
        else
        {
            if (!filedata->Update_TopScore_RecentScore(user))
                return;

            if (!UI->showover(user))
                return;
        }
    }
    else
    {

        if (!UI->DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }

        user->setRow(user->getRow() + 1);

        if (!UI->DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }
    }
}

void EventLoop::handleTimedUserLogic(User* user)
{
    // ��ȡ��ǰʱ��
    user->setCurrentTime(std::chrono::steady_clock::now());

    // ��������ϴδ���������ʱ��
    std::chrono::duration<double> elapsed_time = user->getCurrentTime() - user->getLastTriggerTime();

    // ����ʱ���
    if (elapsed_time >= std::chrono::duration<double>(user->getSpeed()))
    {
        // ִ����Ӧ���߼�����
        processBlockDown(user);

        // ���ϴδ���ʱ�����Ϊ��ǰʱ��
        user->setLastTriggerTime(user->getCurrentTime());
    }
}

void EventLoop::processTimerEvent()
{
    if (!users.empty())
    {
        for (auto i = users.begin(); i != users.end(); )
        {
            // ִ��һЩ�������
            if (i->second->getStatus() == STATUS_OVER_QUIT)
            {
                auto eraseIter = i++;
                //printf("Client[%d] disconnected!\n", eraseIter->second->fd);
                logger->info("Client[{}] disconnected!\n", eraseIter->second->getFd());
                logger->flush();

                close(eraseIter->second->getFd());

                auto it = users.find(eraseIter->second->getFd());
                if (it != users.end())
                {
                    users.erase(eraseIter); // �� map ��ɾ��Ԫ��
                }
                delete eraseIter->second;
            }
            else if (i->second->getStatus() == STATUS_PLAYING)
            {
                handleTimedUserLogic(i->second);

                i++; // �ƶ�����һ��Ԫ��
            }
            else
            {
                i++;
            }
        }
    }

}


void EventLoop::processEvents(int readyCount, epoll_event* events, int epollfd)
{
    for (int i = 0; i < readyCount; ++i)
    {
        User* user = (User*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;

        if (currentFd == serverSocket)
        {
            this->handleNewClientConnection(epollfd);
        }
        else if (currentFd == timerfd)
        {
            this->processTimerEvent();
        }
        else
        {
            this->handleClientData(user);
        }
    }

}

void EventLoop::Run(int epollfd)
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) 
        {
            logger->error("Failed on epoll_wait: {} (errno: {})\n", strerror(errno), errno);
            logger->flush();
            continue;
        }
        processEvents(readyCount, events, epollfd);
    }
}