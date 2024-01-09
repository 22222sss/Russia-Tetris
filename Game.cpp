#include"Game.h"
#include"Public.h"
#include"UserInfo.h"
#include"Public_game.h"
#include"Player.h"
#include"EventLoop.h"
#include"Server.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern map<int, UserInfo*> g_playing_gamer;

extern shared_ptr<spdlog::logger> logger;

int Game::select_game_difficulty(Server* server,UserInfo* user, int epollfd)
{
    int temp = server->ReceiveData(user,epollfd);
    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "1")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 1;

            return 1;
        }
        else if (user->receivedata == "2")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 0.5;

            return 2;
        }
        else if (user->receivedata == "3")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 0.2;

            return 3;
        }
        else
        {
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 7, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
                return -1;
            user->receivedata = "";
        }
    }
    return 4;
}

bool Game::InitInterface(UserInfo* user)
{
    for (int i = 0; i < WINDOW_ROW_COUNT; i++)
    {
        for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
        {
            if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
            {
                user->data[i][j] = 1; //标记该位置有方块

                if (!outputText(user,i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else if (i == WINDOW_ROW_COUNT - 1)
            {
                user->data[i][j] = 1; //标记该位置有方块

                if (!outputText(user,i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else
                user->data[i][j] = 0; //标记该位置无方块
        }
    }

    for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
    {
        user->data[11][i] = 1; //标记该位置有方块
        if (!outputText(user,11 + 1, 2 * i + 1, COLOR_WHITE, "■"))
            return false;
    }

    if (!outputText(user,2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
        return false;


    if (!outputText(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
        return false;

    return true;
}

bool Game::InitGameFace(UserInfo* user)
{

    if (!clear(user))
        return false;

    if (!this->InitInterface(user))//初始化界面
    {
        //printf("Client[%d] InitInterface Error In handleNewClientConnection\n", fd);
        //logger->error("Client[{}] InitInterface Error In handleNewClientConnection\n", user->fd);
        //logger->flush();
        return false;
    }

    InitBlockInfo(); //初始化方块信息
    srand((unsigned int)time(NULL)); //设置随机数生成的起点
    user->shape = rand() % 7;
    user->form = rand() % 4; //随机获取方块的形状和形态
    user->nextShape = rand() % 7;
    user->nextForm = rand() % 4;
    //随机获取下一个方块的形状和形态
    user->row = 1;
    user->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置

    if (!this->DrawBlock(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
    {
        return false;
    }

    if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) //将该方块显示在初始下落位置
    {
        return false;
    }
    return true;
}

bool Game::DrawBlock(UserInfo* user,int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user,row + i, 2 * (col + j) - 1, Color(shape), "■"))
                    return false;
            }
        }
    }
    return true;
}

bool Game::DrawSpace(UserInfo* user,int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user,row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

bool Game::IsLegal(UserInfo* user,int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((blockDefines[shape][form].space[i][j] == 1) && (user->data[row + i - 1][col + j - 1] == 1))
                return false;
        }
    }
    return true;
}

int Game::Is_Increase_Score(UserInfo* user)
{
    int i = 0, j = 0;
    //判断是否得分
    for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
    {
        int sum = 0;
        for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
        {
            sum += user->data[i][j];
        }
        if (sum == 0)
            break;
        if (sum == WINDOW_COL_COUNT - 2)//该行全是方块，可得分
        {
            user->line++;
            for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//清除得分行的方块信息
            {
                user->data[i][j] = 0;
                if (!outputText(user,i + 1, 2 * j + 1, COLOR_WHITE, "  "))
                    return -1;

            }
            //把被清除行上面的行整体向下挪一格
            for (int m = i; m > 1; m--)
            {
                sum = 0;//记录上一行的方块个数
                for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
                {
                    sum += user->data[m - 1][n];//统计上一行的方块个数
                    user->data[m][n] = user->data[m - 1][n];//将上一行方块的标识移到下一行
                    user->color[m][n] = user->color[m - 1][n];//将上一行方块的颜色编号移到下一行
                    if (user->data[m][n] == 1)
                    {
                        if (!outputText(user,m + 1, 2 * n + 1, Color(user->color[m][n]), "■"))
                            return -1;
                    }
                    else
                    {
                        if (!outputText(user,m + 1, 2 * n + 1, COLOR_WHITE, "  "))
                            return -1;
                    }
                }
                if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
                    return 1; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
            }
        }
    }
    return 0;
}

bool Game::UpdateCurrentScore(UserInfo* user)
{
    if (user->line >= 2)
    {
        user->score += (user->line + 1) * 10;

        if (!outputgrade(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->score))
        {
            return false;
        }
    }
    else
    {
        user->score += user->line * 10;

        if (!outputgrade(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->score))
        {
            return false;
        }
    }
    return true;
}

bool Game::IsOver(UserInfo* user)
{
    //判断游戏是否结束
    for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
    {
        if (user->data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
        {
            user->status = STATUS_OVER_CONFIRMING;
            return true;
        }
    }
    return false;
}

bool Game::showover(UserInfo* user)
{
    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

void Game::resetUserInfo(UserInfo* user)
{
    user->line = 0;
    user->score = 0;
    user->username = "";
    user->status = STATUS_NOTSTART;
    user->password = "";
    user->Maximum_score = 0;
    user->scores = {};
    user->timestamp = "";
    memset(user->data, 0, sizeof(user->data));
    memset(user->color, -1, sizeof(user->color));
}





void Game::processBlockDown(UserInfo* user)
{
    if (this->IsLegal(user,user->shape, user->form, user->row + 1, user->col) == 0)
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
            if (this->Is_Increase_Score(user) == 1)
            {
                continue;
            }
            else if (this->Is_Increase_Score(user) == 0)
            {
                break;
            }
            else if (this->Is_Increase_Score(user) == -1)
            {
                return;
            }
        }

        if (!this->UpdateCurrentScore(user))
        {
            return;
        }

        if (!this->IsOver(user))//判断是否结束
        {
            user->shape = user->nextShape;
            user->form = user->nextForm;

            if (!this->DrawSpace(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->nextShape = rand() % 7;
            user->nextForm = rand() % 4;

            user->row = 1;
            user->col = WINDOW_COL_COUNT / 2 - 1;

            if (!this->DrawBlock(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
            {
                return;
            }

            if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))//将该方块显示在初始下落位置
            {
                return;
            }
        }
        else
        {
            if (!Update_TopScore_RecentScore(user))
                return;

            if (!this->showover(user))
                return;
        }
    }
    else
    {

        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col))
        {
            return;
        }

        user->row++;

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))
        {
            return;
        }
    }
}

void Game::handleTimedUserLogic(UserInfo* user)
{
    // 记录上次触发时间
    //this->lastTriggerTime = std::chrono::steady_clock::now();

    // 获取当前时间
    user->currentTime = std::chrono::steady_clock::now();

    // 计算距离上次触发经过的时间
    std::chrono::duration<double> elapsed_time = user->currentTime - user->lastTriggerTime;

    // 计算时间差
    if (elapsed_time >= std::chrono::duration<double>(user->speed))
    {
        // 执行相应的逻辑处理
        this->processBlockDown(user);

        // 将上次触发时间更新为当前时间
        user->lastTriggerTime = user->currentTime;
    }
}

void Game::processTimerEvent(int epollfd)
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
            //printf("Client[%d] disconnected!\n", eraseIter->second->fd);
            logger->info("Client[{}] disconnected!\n", eraseIter->second->fd);
            logger->flush();
            close(eraseIter->second->fd);

            auto it = g_playing_gamer.find(eraseIter->second->fd);
            if (it != g_playing_gamer.end()) {
                g_playing_gamer.erase(eraseIter); // 从 map 中删除元素
            }
            delete eraseIter->second;

            if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr)) {
                perror("epoll_ctl EPOLL_CTL_DEL");
                logger->error("epoll_ctl EPOLL_CTL_DEL: {}", strerror(errno));
                logger->flush();
            }
        }
        else
        {
            this->handleTimedUserLogic(i->second);

            i++; // 移动到下一个元素
        }
    }
}



//STATUS_PLAYING


bool Game::process_STATUS_PLAYING(UserInfo* user)
{
    char buffer[1024];
    int bytesRead = recv(user->fd, buffer, sizeof(buffer), 0);

    if (bytesRead == -1 || bytesRead == 0) {
        this->handleRecvError_STATUS_PLAYING(user);
        return false;
    }

    if (!this->handleReceivedData_STATUS_PLAYING(buffer, user))
        return false;
    return true;
}

void Game::handleRecvError_STATUS_PLAYING(UserInfo* user)
{
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
    Update_TopScore_RecentScore(user);
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
    logger->flush();
}

bool Game::handleMoveDown(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, user->form, user->row + 1, user->col) == 1) //判断方块向下移动一位后是否合法
    {
        //方块下落后合法才进行以下操作
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) {
            return false;
        }
    }

    return true;
}

bool Game::handleMoveLeft(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, user->form, user->row, user->col - 1) == 1) //判断方块向左移动一位后是否合法
    {
        //方块左移后合法才进行以下操作
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) 
        {
            return false;
        }
    }

    return true;
}

bool Game::handleMoveRight(UserInfo* user)
{
    if (this->IsLegal(user,user->shape, user->form, user->row, user->col + 1) == 1) //判断方块向右移动一位后是否合法
    {
        //方块右移后合法才进行以下操作
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col))//用空格覆盖当前方块所在位置
        {
            return false;
        }
        user->col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))
        {
            return false;
        }
    }

    return true;
}

bool Game::handleRotation(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, (user->form + 1) % 4, user->row + 1, user->col) == 1) //判断方块旋转后是否合法 
    {
        //方块旋转后合法才进行以下操作

        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //用空格覆盖当前方块所在位置 
        {
            return false;
        }

        user->row++; //纵坐标自增（总不能原地旋转吧）
        user->form = (user->form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) {
            return false;
        }
    }

    return true;
}

bool Game::handleReceivedData_STATUS_PLAYING(char* buffer, UserInfo* user)
{
    // 处理接收到的数据
    if (strcmp(buffer, KEY_DOWN) == 0)//下
    {
        if (!this->handleMoveDown(user)) //判断方块向下移动一位后是否合法
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_LEFT) == 0)//左
    {
        if (!this->handleMoveLeft(user))
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_RIGHT) == 0)//右
    {
        if (!this->handleMoveRight(user))
        {
            return false;
        }
    }
    else if (*buffer == ' ')
    {
        if (!this->handleRotation(user))
        {
            return false;
        }
    }

    return true;
}


//STATUS_OVER_CONFIRMING


void Game::handleRecvError_STATUS_OVER_CONFIRMING(UserInfo* user) {
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
    logger->flush();
}

void Game::handleDisconnect_STATUS_OVER_CONFIRMING(UserInfo* user) {
    logger->error("Client[{}] disconnect!\n", user->fd);
    logger->flush();
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
}

bool Game::handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, Server* server, UserInfo* user, int epollfd)
{
    if (*buffer == 'Y' || *buffer == 'y')
    {
        if (!server->showGameDifficulty(user, epollfd))
        {
            return false;
        }
        user->status = STATUS_SELECT_GAME_DIFFICULTY;
        user->score = 0;
    }
    else if (*buffer == 'n' || *buffer == 'N')
    {
        auto it = g_playing_gamer.find(user->fd);
        if (it != g_playing_gamer.end())
        {
            g_playing_gamer.erase(it);
        }
        this->resetUserInfo(user);
        if (!server->showInitMenu(user))
            return false;
    }
    else
    {
        if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "选择错误，请再次选择"))
            return false;
    }
    return true;
}

bool Game::process_STATUS_OVER_CONFIRMING(Server* server,UserInfo* user, int epollfd)
{
    char buffer[1024];
    int bytesRead = recv(user->fd, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        this->handleRecvError_STATUS_OVER_CONFIRMING(user);
        return false;
    }
    else if (bytesRead == 0) {
        this->handleDisconnect_STATUS_OVER_CONFIRMING(user);
        return false;
    }

    if (!this->handleBufferData_STATUS_OVER_CONFIRMING(buffer, server, user, epollfd))
        return false;

    return true;

}

//STATUS_SELECT_GAME_DIFFICULTY


bool Game::process_STATUS_SELECT_GAME_DIFFICULTY(Server *server, UserInfo* user, int epollfd)
{
    int key = this->select_game_difficulty(server,user,epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    else if (key == 2)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    else if (key == 3)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    return true;
}

