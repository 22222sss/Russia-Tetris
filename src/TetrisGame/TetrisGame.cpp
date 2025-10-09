#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern shared_ptr<spdlog::logger> logger;


int TetrisGame::select_game_difficulty(User* user)
{
    
    if (!Filedata::loadPlayerData())
        return false;


    int temp = Server::ReceiveData(user);
    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() == "1")
        {
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("EASY");
            
            user->setSpeed(1);


            user->initUserInfo();

            return 1;
        }
        else if (user->getReceivedata() == "2")
        {
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("NORMAL");

            user->setSpeed(0.5);

            user->initUserInfo();
            
            return 2;
        }
        else if (user->getReceivedata() == "3")
        {
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("DIFFICULT");

            user->setSpeed(0.2);

            user->initUserInfo();

            return 3;
        }
        else if (user->getReceivedata() == "4")
        {
            if (!UImanage::showLoadMenu(user))
                return false;

            user->setStatus(STATUS_LOGIN);
            user->setReceivedata("");
            
            return 4;
        }
        else
        {
            if (!UImanage::show_Error_Message(WINDOW_ROW_COUNT / 2 + 10, user))
                return false;

            user->setReceivedata("");
        }
    }
    return 5;
}

bool TetrisGame::IsLegal(User* user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((blockDefines[shape][form].space[i][j] == 1) && (user->getData(row + i - 1, col + j - 1) == 1))
                return false;
        }
    }
    return true;
}

int TetrisGame::Is_Increase_Score(User* user)
{
    int i = 0, j = 0;
    //判断是否得分
    for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
    {
        int sum = 0;
        for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
        {
            sum += user->getData(i, j);
        }
        if (sum == 0)
            break;
        if (sum == WINDOW_COL_COUNT - 2)//该行全是方块，可得分
        {
            user->setLine(user->getLine() + 1);

            for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//清除得分行的方块信息
            {
                user->setData(i, j, 0);
                if (!outputText(user, i + 1, 2 * j + 1, COLOR_WHITE, "  "))
                    return -1;

            }
            //把被清除行上面的行整体向下挪一格
            for (int m = i; m > 1; m--)
            {
                sum = 0;//记录上一行的方块个数
                for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
                {
                    sum += user->getData(m - 1, n);//统计上一行的方块个数
                    user->setData(m, n, user->getData(m - 1, n));//将上一行方块的标识移到下一行
                    user->setColor(m, n, user->getColor(m - 1, n));//将上一行方块的颜色编号移到下一行
                    if (user->getData(m, n) == 1)
                    {
                        if (!outputText(user, m + 1, 2 * n + 1, Color(user->getColor(m, n)), "■"))
                            return -1;
                    }
                    else
                    {
                        if (!outputText(user, m + 1, 2 * n + 1, COLOR_WHITE, "  "))
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

bool TetrisGame::UpdateCurrentScore(User* user)
{
    if (user->getLine() >= 2)
    {
        user->setScore(user->getScore()+(user->getLine() + 1) * 10);

        if (!outputgrade(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    else
    {
        user->setScore(user->getScore()+user->getLine() * 10);

        if (!outputgrade(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    return true;
}

bool TetrisGame::IsOver(User* user)
{
    //判断游戏是否结束
    for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
    {
        if (user->getData(1, j) == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
        {
            user->setStatus(STATUS_OVER_CONFIRMING);
            return true;
        }
    }
    return false;
}



//STATUS_PLAYING


bool TetrisGame::process_STATUS_PLAYING(User* user)
{
    char buffer[1024] = { 0 };
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1 || bytesRead == 0) {
        TetrisGame::handleRecvError_STATUS_PLAYING(user);
        return false;
    }

    buffer[bytesRead] = '\0';

    if (!TetrisGame::handleReceivedData_STATUS_PLAYING(buffer, user))
        return false;
    return true;
}

void TetrisGame::handleRecvError_STATUS_PLAYING(User* user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

bool TetrisGame::handleMoveDown(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 1) //判断方块向下移动一位后是否合法
    {
        //方块下落后合法才进行以下操作
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//纵坐标自增（下一次显示方块时就相当于下落了一格了）

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleMoveLeft(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() - 1) == 1) //判断方块向左移动一位后是否合法
    {
        //方块左移后合法才进行以下操作
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->setCol(user->getCol() - 1); //横坐标自减（下一次显示方块时就相当于左移了一格了）

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleMoveRight(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() + 1) == 1) //判断方块向右移动一位后是否合法
    {
        //方块右移后合法才进行以下操作
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//用空格覆盖当前方块所在位置
        {
            return false;
        }
        user->setCol(user->getCol() + 1); //横坐标自增（下一次显示方块时就相当于右移了一格了）

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleRotation(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), (user->getForm() + 1) % 4, user->getRow() + 1, user->getCol()) == 1) //判断方块旋转后是否合法 
    {
        //方块旋转后合法才进行以下操作

        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置 
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//纵坐标自增（总不能原地旋转吧）
        user->setForm((user->getForm() + 1) % 4); //方块的形态自增（下一次显示方块时就相当于旋转了）

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleReceivedData_STATUS_PLAYING(char* buffer, User* user)
{
    // 处理接收到的数据
    if (strcmp(buffer, KEY_DOWN) == 0)//下
    {
        if (!TetrisGame::handleMoveDown(user)) //判断方块向下移动一位后是否合法
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_LEFT) == 0)//左
    {
        if (!TetrisGame::handleMoveLeft(user))
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_RIGHT) == 0)//右
    {
        if (!TetrisGame::handleMoveRight(user))
        {
            return false;
        }
    }
    else if (*buffer == ' ')
    {
        if (!TetrisGame::handleRotation(user))
        {
            return false;
        }
    }

    return true;
}


//STATUS_OVER_CONFIRMING


void TetrisGame::handleRecvError_STATUS_OVER_CONFIRMING(User* user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

void TetrisGame::handleDisconnect_STATUS_OVER_CONFIRMING(User* user) 
{

    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] disconnect!\n", user->getFd());
    logger->flush();
}

bool TetrisGame::handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, User* user)
{

    if (*buffer == 'Y' || *buffer == 'y')
    {
        if (!UImanage::showGameDifficulty(user))
        {
            return false;
        }

        user->setStatus(STATUS_SELECT_GAME_DIFFICULTY);
        user->setScore(0);
        user->setScores({});
        user->setGameDiffclutys({});
    }
    else if (*buffer == 'n' || *buffer == 'N')
    {
        user->resetUserInfo();

        user->setStatus(STATUS_NOTSTART);
        if (!UImanage::showInitMenu(user))
            return false;

    }
    else
    {
        if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "选择错误，请再次选择"))
            return false;
    }
    return true;
}

bool TetrisGame::process_STATUS_OVER_CONFIRMING(User* user)
{
    char buffer[1024];
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        TetrisGame::handleRecvError_STATUS_OVER_CONFIRMING(user);
        return false;
    }
    else if (bytesRead == 0) {
        TetrisGame::handleDisconnect_STATUS_OVER_CONFIRMING(user);
        return false;
    }

    if (!TetrisGame::handleBufferData_STATUS_OVER_CONFIRMING(buffer, user))
        return false;

    return true;

}

//STATUS_SELECT_GAME_DIFFICULTY


bool TetrisGame::process_STATUS_SELECT_GAME_DIFFICULTY(User* user)
{
    int key = TetrisGame::select_game_difficulty(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 2)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 3)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 4)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_LOGIN);
    }
    return true;
}

//处理时间事件机制--键盘不输入时方块自动下降
void TetrisGame::processBlockDown(User* user)
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

void TetrisGame::handleTimedUserLogic(User* user)
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

void TetrisGame::processTimerEvent(int timerfd, short events, void* arg)
{
    if (!User::getUsers().empty())
    {
        for (auto i = User::getUsers().begin(); i != User::getUsers().end(); i++)
        {
            if (i->second->getStatus() == STATUS_PLAYING)
            {
                handleTimedUserLogic(i->second);
            }
        }
    }
}
