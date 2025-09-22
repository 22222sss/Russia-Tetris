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

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern map<int, User*> users;

extern shared_ptr<spdlog::logger> logger;

extern map<evutil_socket_t, struct event*> event;

//在EventLoop类中设置这些回调函数，并在事件发生时调用它们

void handleNewClientConnection(int serverSocket, short events, void* arg)
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

    // 创建新的用户信息结构体
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

    // 创建并添加客户端数据事件
    struct event_base* base = (event_base*)arg;

    // 创建并添加客户端套接字事件
    struct event* clientEvent = event_new(base, clientSocket, EV_READ | EV_PERSIST, handleClientData, (void*)newUser);

    event.insert(make_pair(clientSocket, clientEvent));

    event_add(clientEvent, NULL);

    // 设置客户端连接为非阻塞模式
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

   

    //std::unique_ptr<UImanage> UI(new UImanage);

    if (!UImanage::showInitMenu(newUser))
        return;
}

void handleClientData(int clientSocket, short events, void* arg)
{
    User* user = (User*)arg;

    //std::unique_ptr<Server> server(new Server);

    //std::unique_ptr<TetrisGame> game(new TetrisGame);

    // 处理已连接客户端的数据接收事件
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!TetrisGame::process_STATUS_PLAYING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_OVER_CONFIRMING)
    {
        if (!TetrisGame::process_STATUS_OVER_CONFIRMING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_NOTSTART)
    {
        if (!Server::process_STATUS_NOTSTART(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_USERNAME_REGISTER)
    {
        if (!Server::process_STATUS_RECEIVE_USERNAME_REGISTER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_PASSWORD_REGISTER)
    {
        if (!Server::process_STATUS_RECEIVE_PASSWORD_REGISTER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_USERNAME_LOAD)
    {
        if (!Server::process_STATUS_RECEIVE_USERNAME_LOAD(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_RECEIVE_PASSWORD_LOAD)
    {
        if (!Server::process_STATUS_RECEIVE_PASSWORD_LOAD(user))
        {
            return;
        }

    }
    else if (user->getStatus() == STATUS_LOGIN)
    {
        if (!Server::process_STATUS_LOGIN(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_SELECT_GAME_DIFFICULTY)
    {
        if (!TetrisGame::process_STATUS_SELECT_GAME_DIFFICULTY(user))
        {
            return;
        }

    }
    else if (user->getStatus() == STATUS_LOGIN_OVER)
    {
        if (!Server::process_STATUS_LOGIN_OVER(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_REGISTER_OR_LOAD_OVER)
    {
        if (!Server::process_STATUS_REGISTER_OR_LOAD_OVER(user))
        {
            return;
        }
    }
}

void processBlockDown(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 0)
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

        

        user->setLine(0);

        while (1)
        {
            if (TetrisGame::Is_Increase_Score(user) == 1)
            {
                continue;
            }
            else if (TetrisGame::Is_Increase_Score(user) == 0)
            {
                break;
            }
            else if (TetrisGame::Is_Increase_Score(user) == -1)
            {
                return;
            }
        }

        if (!TetrisGame::UpdateCurrentScore(user))
        {
            return;
        }

        if (!TetrisGame::IsOver(user))//判断是否结束
        {
            user->setShape(user->getNextShape());
            user->setForm(user->getNextForm());

            if (!UImanage::DrawSpace(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->setNextShape(rand() % 7);
            user->setNextForm(rand() % 4);

            user->setRow(1);
            user->setCol(WINDOW_COL_COUNT / 2 - 1);

            if (!UImanage::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
            {
                return;
            }

            if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//将该方块显示在初始下落位置
            {
                return;
            }
        }
        else
        {
            if (!Filedata::Update_TopScore_RecentScore(user))
                return;

            if (!UImanage::showover(user))
                return;
        }
    }
    else
    {

        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }

        user->setRow(user->getRow() + 1);

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }
    }
}

void handleTimedUserLogic(User* user)
{
    // 获取当前时间
    user->setCurrentTime(std::chrono::steady_clock::now());

    // 计算距离上次触发经过的时间
    std::chrono::duration<double> elapsed_time = user->getCurrentTime() - user->getLastTriggerTime();

    // 计算时间差
    if (elapsed_time >= std::chrono::duration<double>(user->getSpeed()))
    {
        // 执行相应的逻辑处理
        processBlockDown(user);

        // 将上次触发时间更新为当前时间
        user->setLastTriggerTime(user->getCurrentTime());
    }
}

void processTimerEvent(int timerfd, short events, void* arg)
{
    if (!users.empty())
    {
        for (auto i = users.begin(); i != users.end(); )
        {
            // 执行一些条件检查
            if (i->second->getStatus() == STATUS_OVER_QUIT)
            {
                auto eraseIter = i++;
                //printf("Client[%d] disconnected!\n", eraseIter->second->fd);
                logger->info("Client[{}] disconnected!\n", eraseIter->second->getFd());
                logger->flush();

                // 删除特定文件描述符对应的事件
                auto it1 = event.find(eraseIter->second->getFd());

                if (it1 != event.end())
                {
                    struct event* ev_to_delete = it1->second;
                    event_del(ev_to_delete);
                    event_free(ev_to_delete);
                    event.erase(it1); // 从哈希表中删除对应的映射
                }

                close(eraseIter->second->getFd());

                auto it = users.find(eraseIter->second->getFd());
                if (it != users.end())
                {
                    users.erase(eraseIter); // 从 map 中删除元素
                }
                delete eraseIter->second;
            }
            else if (i->second->getStatus() == STATUS_PLAYING)
            {
                handleTimedUserLogic(i->second);

                i++; // 移动到下一个元素
            }
            else
            {
                i++;
            }
        }
    }
}

