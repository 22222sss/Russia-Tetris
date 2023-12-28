#include"Tetris.h"
#include"UserInfo.h"
#include"Server.h"
#include"Player.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern map<int, UserInfo*> g_playing_gamer;

Server::Server(int serverSocket,int timerfd) : serverSocket(serverSocket), timerfd(timerfd) {}

void Server::handleNewClientConnection(int epollfd)
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

    // 创建新的用户信息结构体
    UserInfo* newUser = new UserInfo(clientSocket);

    // 设置客户端连接为非阻塞模式
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    if (newUser == nullptr) 
    {
        close(clientSocket);
        printf("allocate memory for newUser Error In handleNewClientConnection");
        return;
    }


    // 将新连接的事件添加到 epoll 实例中

    struct epoll_event newEvent;
    newEvent.events = EPOLLIN | EPOLLET; // 监听读事件并将EPOLL设为边缘触发(Edge Triggered)模式，
    newEvent.data.ptr = newUser; // 将指针指向用户信息结构体

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
    {
        printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        close(clientSocket);
        delete newUser;
        return;
    }

    if (!newUser->showInitMenu())
        return;
}

void Server::handleClientData(UserInfo* userInfo,int epollfd)
{
    // 处理已连接客户端的数据接收事件
        if (userInfo->status == STATUS_PLAYING)
        {
            char buffer[1024];
            int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
            if (bytesRead == -1) {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                userInfo->Update_TopScore_RecentScore();
                printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
                return;
            }
            else if (bytesRead == 0) {
                // 客户端连接已关闭
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                userInfo->Update_TopScore_RecentScore();
                return;
            }


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
            char buffer[1024];
            int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
            if (bytesRead == -1) {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
                return;
            }
            else if (bytesRead == 0) {
                printf("Client[%d] disconnect!\n", userInfo->fd);
                // 客户端连接已关闭
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                return;
            }


            if (*buffer == 'Y' || *buffer == 'y')
            {

                if (!userInfo->showGameDifficulty(epollfd))
                {
                    return ;
                }

                userInfo->status = STATUS_SELECT_GAME_DIFFICULTY;

                userInfo->score = 0;
            }
            else if (*buffer == 'n' || *buffer == 'N')
            {

                // 在这里删除 g_playing_gamer 的元素
                auto it = g_playing_gamer.find(userInfo->fd);
                if (it != g_playing_gamer.end()) {
                    g_playing_gamer.erase(it); // 从 map 中删除元素
                }

                userInfo->resetUserInfo();

                if(!userInfo->showInitMenu())
                    return;
            }
            else
            {
                if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "选择错误，请再次选择"))
                    return;
            }
        }
        else if (userInfo->status == STATUS_NOTSTART)
        {
            int temp = userInfo->ReceiveData(epollfd);
            if (temp == -1)
            {
                delete userInfo;
                return;
            }
            else if (temp == 1)
            {
                if (userInfo->receivedata == "1")
                {   
                    if (!userInfo->registerUser(epollfd))
                        return;
                     
                    userInfo->status = STATUS_RECEIVE_USERNAME_REGISTER;
                    userInfo->receivedata = "";
                }
                else if (userInfo->receivedata == "2")
                {
                    if (!userInfo->loadUser(epollfd))
                        return;

                    userInfo->status = STATUS_RECEIVE_USERNAME_LOAD;
                    userInfo->receivedata = "";
                }
                else
                {
                    userInfo->receivedata = "";
                    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
                    if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 1, COLOR_WHITE, emptyLine))
                        return;
                    if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
                        return;
                }
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_USERNAME_REGISTER)
        {
            int key = userInfo->receive_username_register(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_RECEIVE_PASSWORD_REGISTER;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_PASSWORD_REGISTER)
        {
            int key = userInfo->receive_password_register(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_REGISTER_OR_LOAD_OVER;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_USERNAME_LOAD)
        {
            int key = userInfo->receive_username_load(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_RECEIVE_PASSWORD_LOAD;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_PASSWORD_LOAD)
        {
            int key = userInfo->receive_password_load(epollfd);

            if (key == -1)
            {
                return; 
            }
            else if (key == 1)
            {
                if (!userInfo->showLoadMenu())
                    return;
                userInfo->status = STATUS_LOGIN;
                userInfo->receivedata = "";
            }
            else if (key==0)
            {
                userInfo->status = STATUS_REGISTER_OR_LOAD_OVER;
                userInfo->receivedata = "";
            }
            
        }
        else if (userInfo->status == STATUS_LOGIN)
        {
            int key = userInfo->loginUser(epollfd);

            if (key==-1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_LOGIN_OVER;
            }
            else if (key == 2)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_LOGIN_OVER;
            }
            else if (key == 3)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_SELECT_GAME_DIFFICULTY;
            }
            else if (key == 4)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_NOTSTART;
            }
        }
        else if (userInfo->status==STATUS_SELECT_GAME_DIFFICULTY)
        {
            int key = userInfo->select_game_difficulty(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }
            else if (key == 2)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }
            else if (key == 3)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }

        }
        else if (userInfo->status == STATUS_LOGIN_OVER)
        {
            int key = userInfo->returnToLoadMenu(WINDOW_ROW_COUNT / 3 + 20, epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_LOGIN;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_REGISTER_OR_LOAD_OVER)
        {
            int key = userInfo->returnToInitMenu(epollfd);
            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_NOTSTART;
                userInfo->receivedata = "";
            }
        }
}

void Server::processTimerEvent(int epollfd)
{
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

            auto it = g_playing_gamer.find(eraseIter->second->fd);
            if (it != g_playing_gamer.end()) {
                g_playing_gamer.erase(eraseIter); // 从 map 中删除元素
            }
            delete eraseIter->second;

            if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr)) {
                perror("epoll_ctl EPOLL_CTL_DEL");
            }
        }
        else
        {
            i->second->handleTimedUserLogic();

            i++; // 移动到下一个元素
        }
    }
}

void Server::processEvents(int readyCount, epoll_event* events, int epollfd)
{
    for (int i = 0; i < readyCount; ++i)
    {
        UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;

        if (currentFd == serverSocket)
        {
            handleNewClientConnection(epollfd);
        }
        else if (currentFd == timerfd)
        {
            if (!g_playing_gamer.empty())
            {
                processTimerEvent(epollfd);
            }
        }
        else
        {
            handleClientData(userInfo,epollfd);
        }
    }

}

void Server::Run(int epollfd)
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) {
            printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
            continue;
        }
        processEvents(readyCount, events ,epollfd);
    }
}