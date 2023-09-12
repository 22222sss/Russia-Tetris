#include"Tetris.h"
#include"UserInfo.h"
#include"Server.h"



extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern map<int, UserInfo*> g_playing_gamer;

Server::Server(int serverSocket, int epollfd, int timerfd): serverSocket(serverSocket) , epollfd(epollfd), timerfd(timerfd){}

void Server::handleNewClientConnection()
{
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1)
    {
        printf("accept Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        return;
    }
    else
    {
        printf("Client[%d], welcome!\n", clientSocket);
        //Client.push_back(client);
    }

    // 设置客户端连接为非阻塞模式
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    // 创建新的用户信息结构体
    UserInfo* newUser = new UserInfo(clientSocket, STATUS_PLAYING, epollfd);

    if (newUser == nullptr) {
        close(clientSocket);
        printf("allocate memory for newUser Error In handleNewClientConnection");
        return;
    }

    g_playing_gamer.insert(make_pair(newUser->fd, newUser));

    // 将新连接的事件添加到 epoll 实例中

    struct epoll_event newEvent;
    newEvent.events = EPOLLIN | EPOLLET; // 监听读事件并将EPOLL设为边缘触发(Edge Triggered)模式，
    newEvent.data.ptr = newUser; // 将指针指向用户信息结构体

    if (epoll_ctl(newUser->epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
    {
        printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        close(clientSocket);
        g_playing_gamer.erase(newUser->fd);
        delete newUser;
        return;
    }

    if (!newUser->InitInterface())//初始化界面
    {
        printf("Client[%d] InitInterface Error In handleNewClientConnection\n", newUser->fd);
        return;
    }
    InitBlockInfo(); //初始化方块信息
    srand((unsigned int)time(NULL)); //设置随机数生成的起点
    newUser->shape = rand() % 7;
    newUser->form = rand() % 4; //随机获取方块的形状和形态
    newUser->nextShape = rand() % 7;
    newUser->nextForm = rand() % 4;
    //随机获取下一个方块的形状和形态
    newUser->row = 1;
    newUser->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置



    if (!newUser->DrawBlock(newUser->nextShape, newUser->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
    {
        printf("Client[%d] Draw next Block Error In handleNewClientConnection\n", newUser->fd);
        return;
    }



    if (!newUser->DrawBlock(newUser->shape, newUser->form, newUser->row, newUser->col)) //将该方块显示在初始下落位置
    {
        printf("Client[%d] Draw Falling Block Error In handleNewClientConnection\n", newUser->fd);
        return;
    }
}

// 定义处理用户逻辑的函数
void Server::processUserLogic(UserInfo* user)
{
    if (user->IsLegal(user->shape, user->form, user->row + 1, user->col) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (blockDefines[user->shape][user->form].space[i][j] == 1)
                {
                    user->data[user->row + i - 1][user->col + j - 1] = 1;
                    user->color[user->row + i - 1][user->col + j - 1] = user->shape;
                }
            }
        }

        user->line = 0;

        while (1)
        {
            if (user->Is_Increase_Score() == 1)
            {
                continue;
            }
            else if (user->Is_Increase_Score() == 0)
            {
                break;
            }
            else if (user->Is_Increase_Score() == -1)
            {
                return;
            }
        }

        if (!user->UpdateCurrentScore())
        {
            return;
        }

        if (!user->IsOver())//判断是否结束
        {
            user->shape = user->nextShape;
            user->form = user->nextForm;

            if (!user->DrawSpace(user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->nextShape = rand() % 7;
            user->nextForm = rand() % 4;

            user->row = 1;
            user->col = WINDOW_COL_COUNT / 2 - 1;

            if (!user->DrawBlock(user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
            {
                return;
            }

            if (!user->DrawBlock(user->shape, user->form, user->row, user->col))//将该方块显示在初始下落位置
            {
                return;
            }
        }

        else
        {
            if (!user->showover())
                return;
        }
    }
    else
    {

        if (!user->DrawSpace(user->shape, user->form, user->row, user->col))
        {
            return;
        }

        user->row++;

        if (!user->DrawBlock(user->shape, user->form, user->row, user->col))
        {
            return;
        }
    }
}

void Server::handleClientData(UserInfo* userInfo)
{
    // 处理已连接客户端的数据接收事件
    char buffer[1024];
    int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
    if (bytesRead == -1) {
        userInfo->status = STATUS_OVER_QUIT;
        close(userInfo->fd);
        printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
    }
    else if (bytesRead == 0) {
        // 客户端连接已关闭
        userInfo->status = STATUS_OVER_QUIT;
        close(userInfo->fd);
    }
    else
    {
        if (userInfo->status == STATUS_PLAYING)
        {
            // 处理接收到的数据
            if (strcmp(buffer, KEY_DOWN) == 0)//下
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row + 1, userInfo->col) == 1) //判断方块向下移动一位后是否合法
                {
                    //方块下落后合法才进行以下操作
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
                    {
                        return;
                    }

                    userInfo->row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (strcmp(buffer, KEY_LEFT) == 0)//左
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row, userInfo->col - 1) == 1) //判断方块向左移动一位后是否合法
                {
                    //方块左移后合法才进行以下操作
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
                    {
                        return;
                    }

                    userInfo->col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (strcmp(buffer, KEY_RIGHT) == 0)//右
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row, userInfo->col + 1) == 1) //判断方块向右移动一位后是否合法
                {
                    //方块右移后合法才进行以下操作
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
                    {
                        return;
                    }
                    userInfo->col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (*buffer == ' ')
            {
                if (userInfo->IsLegal(userInfo->shape, (userInfo->form + 1) % 4, userInfo->row + 1, userInfo->col) == 1) //判断方块旋转后是否合法
                {
                    //方块旋转后合法才进行以下操作
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
                    {
                        return;
                    }

                    userInfo->row++; //纵坐标自增（总不能原地旋转吧）
                    userInfo->form = (userInfo->form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
        }
        else if (userInfo->status == STATUS_OVER_CONFIRMING)
        {
            if (*buffer == 'Y' || *buffer == 'y')
            {
                auto it = find_if(g_playing_gamer.begin(), g_playing_gamer.end(), [userInfo](const pair<int, UserInfo*>& element) {
                    return element.second == userInfo;
                    });

                if (it == g_playing_gamer.end())
                {
                    userInfo->status = STATUS_PLAYING;

                    g_playing_gamer.insert(make_pair(userInfo->fd, userInfo));

                    userInfo->clear();

                    userInfo->score = 0;

                    if (!userInfo->InitInterface())//初始化界面
                    {
                        return;
                    }
                    InitBlockInfo(); //初始化方块信息
                    srand((unsigned int)time(NULL)); //设置随机数生成的起点
                    userInfo->shape = rand() % 7;
                    userInfo->form = rand() % 4; //随机获取方块的形状和形态
                    userInfo->nextShape = rand() % 7;
                    userInfo->nextForm = rand() % 4;
                    //随机获取下一个方块的形状和形态
                    userInfo->row = 1;
                    userInfo->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置


                    if (!userInfo->DrawBlock(userInfo->nextShape, userInfo->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
                    {
                        return;
                    }

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col)) //将该方块显示在初始下落位置
                    {
                        return;
                    }
                }
            }
            else if (*buffer == 'n' || *buffer == 'N')
            {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                printf("Client[%d] disconnected!\n", userInfo->fd);
            }
            else
            {
                if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "选择错误，请再次选择"))
                    return;
            }
        }
    }
}

void Server::processTimerEvent()
{
    if (!g_playing_gamer.empty())
    {
            // 记录上次触发时间
            static std::chrono::steady_clock::time_point lastTriggerTime = std::chrono::steady_clock::now();

            // 获取当前时间
            std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();

            // 计算距离上次触发经过的时间
            std::chrono::duration<double> elapsed_time = currentTime - lastTriggerTime;

            // 如果时间差大于等于1秒
            if (elapsed_time >= std::chrono::seconds(1))
            {
                // 执行相应的逻辑处理

                for (auto i = g_playing_gamer.begin(); i != g_playing_gamer.end(); )
                {
                    // 执行一些条件检查
                    if (i->second->status == STATUS_OVER_CONFIRMING)
                    {
                        auto eraseIter = i++;
                        g_playing_gamer.erase(eraseIter); // 先使用后缀递增运算符，然后再删除元素
                    }
                    else if (i->second->status == STATUS_OVER_QUIT)
                    {
                        auto eraseIter = i++;
                        printf("Client[%d] disconnected!\n", eraseIter->second->fd);
                        close(eraseIter->second->fd);
                        g_playing_gamer.erase(eraseIter);// 先使用后缀递增运算符，然后再删除元素
                        delete eraseIter->second;
                        epoll_ctl(eraseIter->second->epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr);
                    }
                    else
                    {
                        processUserLogic(i->second);
                        i++; // 移动到下一个元素
                    }
                }
                // 将上次触发时间更新为当前时间
                lastTriggerTime = currentTime;
            }
    }
    
}

void Server::processEvents(int readyCount, epoll_event* events)
{
    for (int i = 0; i < readyCount; ++i)
    {
        UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;
        if (currentFd == serverSocket)
        {
            handleNewClientConnection();
        }
        else if (currentFd == timerfd)
        {
            processTimerEvent();
        }
        else
        {
            handleClientData(userInfo);
        }
    }
}

void Server::Run()
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) {
            printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
            continue;
        }
        processEvents(readyCount, events);
    }
}