#include"Game.h"

extern shared_ptr<spdlog::logger> logger;

void Game::InitBlockInfo()
{
    int i;
    //“T”形
    auto& spaceT = Game::blockDefines[SHAPE_T][0].space;
    for (i = 0; i <= 2; i++)
    {
        spaceT[1][i] = 1;
    }
    spaceT[2][1] = 1;

    //“L”形
    auto& spaceL = Game::blockDefines[SHAPE_L][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceL[i][1] = 1;
    }
    spaceL[3][2] = 1;

    //“J”形
    auto& spaceJ = Game::blockDefines[SHAPE_J][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceJ[i][2] = 1;
    }
    spaceJ[3][1] = 1;

    for (int i = 0; i <= 1; i++)
    {
        //“Z”形
        auto& spaceZ = Game::blockDefines[SHAPE_Z][0].space;
        spaceZ[1][i] = 1;
        spaceZ[2][i + 1] = 1;
        //“S”形
        auto& spaceS = Game::blockDefines[SHAPE_S][0].space;
        spaceS[1][i + 1] = 1;
        spaceS[2][i] = 1;
        //“O”形
        auto& spaceO = Game::blockDefines[SHAPE_O][0].space;
        spaceO[1][i + 1] = 1;
        spaceO[2][i + 1] = 1;
    }

    //“I”形
    auto& spaceI = Game::blockDefines[SHAPE_I][0].space;
    for (i = 0; i <= 3; i++)
    {
        spaceI[i][1] = 1;
    }

    for (int shape = 0; shape < 7; shape++)//7种形状
    {
        for (int form = 0; form < 3; form++)//4种形态（已经有了一种，这里每个还需要增加3种）
        {
            int temp[4][4] = { 0 };

            //获取第form种状态
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    temp[i][j] = Game::blockDefines[shape][form].space[i][j];
                }
            }
            //将第form种形态顺时针旋转，得到第form+1种形态
            for (i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    Game::blockDefines[shape][form + 1].space[i][j] = temp[3 - j][i];
                }
            }
        }
    }
}

bool Game::InitInterface(shared_ptr<User>& user)
{
    for (int i = 0; i < WINDOW_ROW_COUNT; i++)
    {
        for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
        {
            if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
            {
                user->setData(i, j, 1); //标记该位置有方块

                if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else if (i == WINDOW_ROW_COUNT - 1)
            {
                user->setData(i, j, 1); //标记该位置有方块

                if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else
                user->setData(i, j, 0); //标记该位置无方块
        }
    }

    for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
    {
        user->setData(11, i, 1);//标记该位置有方块
        if (!outputText(user, 11 + 1, 2 * i + 1, COLOR_WHITE, "■"))
            return false;
    }

    if (!outputText(user, 2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
        return false;


    if (!outputText(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
        return false;

    return true;
}

bool Game::InitGameFace(shared_ptr<User>& user)
{

    if (!UImanage::clear(user))
        return false;

    if (!Game::InitInterface(user))//初始化界面
    {
        return false;
    }

    InitBlockInfo(); //初始化方块信息
    srand((unsigned int)time(NULL)); //设置随机数生成的起点

    user->setShape(rand() % 7);
    user->setForm(rand() % 4); //随机获取方块的形状和形态
    user->setNextShape(rand() % 7);

    user->setNextForm(rand() % 4);
    //随机获取下一个方块的形状和形态
    user->setRow(1);
    user->setCol(WINDOW_COL_COUNT / 2 - 1); //方块初始下落位置

    if (!Game::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
    {
        return false;
    }

    if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //将该方块显示在初始下落位置
    {
        return false;
    }
    return true;
}

bool Game::DrawBlock(shared_ptr<User>& user, int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (Game::blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, Color(shape), "■"))
                    return false;
            }
        }
    }
    return true;
}

bool Game::DrawSpace(shared_ptr<User>& user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (Game::blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

int Game::select_game_difficulty(shared_ptr<User>& user)
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
            if (!Game::InitGameFace(user))
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
            if (!Game::InitGameFace(user))
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
            if (!Game::InitGameFace(user))
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

bool Game::IsLegal(shared_ptr<User>& user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((Game::blockDefines[shape][form].space[i][j] == 1) && (user->getData(row + i - 1, col + j - 1) == 1))
                return false;
        }
    }
    return true;
}

int Game::Is_Increase_Score(shared_ptr<User>& user)
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

bool Game::UpdateCurrentScore(shared_ptr<User>& user)
{
    if (user->getLine() >= 2)
    {
        user->setScore(user->getScore()+(user->getLine() + 1) * 10);

        if (!outputText(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    else
    {
        user->setScore(user->getScore()+user->getLine() * 10);

        if (!outputText(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    return true;
}

bool Game::IsOver(shared_ptr<User>& user)
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


bool Game::process_STATUS_PLAYING(shared_ptr<User>& user)
{
    char buffer[1024] = { 0 };
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1 || bytesRead == 0) {
        Game::handleRecvError_STATUS_PLAYING(user);
        return false;
    }

    buffer[bytesRead] = '\0';

    if (!Game::handleReceivedData_STATUS_PLAYING(buffer, user))
        return false;
    return true;
}

void Game::handleRecvError_STATUS_PLAYING(shared_ptr<User>& user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

bool Game::handleMoveDown(shared_ptr<User>& user)
{

    if (Game::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 1) //判断方块向下移动一位后是否合法
    {
        //方块下落后合法才进行以下操作
        if (!Game::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//纵坐标自增（下一次显示方块时就相当于下落了一格了）

        if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }

    }

    return true;
}

bool Game::handleMoveLeft(shared_ptr<User>& user)
{

    if (Game::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() - 1) == 1) //判断方块向左移动一位后是否合法
    {
        //方块左移后合法才进行以下操作
        if (!Game::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置
        {
            return false;
        }

        user->setCol(user->getCol() - 1); //横坐标自减（下一次显示方块时就相当于左移了一格了）

        if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }
    }

    return true;
}

bool Game::handleMoveRight(shared_ptr<User>& user)
{

    if (Game::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() + 1) == 1) //判断方块向右移动一位后是否合法
    {
        //方块右移后合法才进行以下操作
        if (!Game::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//用空格覆盖当前方块所在位置
        {
            return false;
        }
        user->setCol(user->getCol() + 1); //横坐标自增（下一次显示方块时就相当于右移了一格了）

        if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }

    }

    return true;
}

bool Game::handleRotation(shared_ptr<User>& user)
{

    if (Game::IsLegal(user, user->getShape(), (user->getForm() + 1) % 4, user->getRow() + 1, user->getCol()) == 1) //判断方块旋转后是否合法 
    {
        //方块旋转后合法才进行以下操作

        if (!Game::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //用空格覆盖当前方块所在位置 
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//纵坐标自增（总不能原地旋转吧）
        user->setForm((user->getForm() + 1) % 4); //方块的形态自增（下一次显示方块时就相当于旋转了）

        if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }
    }

    return true;
}

bool Game::handleReceivedData_STATUS_PLAYING(char* buffer, shared_ptr<User>& user)
{
    // 处理接收到的数据
    if (strcmp(buffer, KEY_DOWN) == 0)//下
    {
        if (!Game::handleMoveDown(user)) //判断方块向下移动一位后是否合法
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_LEFT) == 0)//左
    {
        if (!Game::handleMoveLeft(user))
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_RIGHT) == 0)//右
    {
        if (!Game::handleMoveRight(user))
        {
            return false;
        }
    }
    else if (*buffer == ' ')
    {
        if (!Game::handleRotation(user))
        {
            return false;
        }
    }

    return true;
}


//STATUS_OVER_CONFIRMING


void Game::handleRecvError_STATUS_OVER_CONFIRMING(shared_ptr<User>& user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

void Game::handleDisconnect_STATUS_OVER_CONFIRMING(shared_ptr<User>& user)
{

    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] disconnect!\n", user->getFd());
    logger->flush();
}

bool Game::handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, shared_ptr<User>& user)
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

bool Game::process_STATUS_OVER_CONFIRMING(shared_ptr<User>& user)
{
    char buffer[1024];
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        Game::handleRecvError_STATUS_OVER_CONFIRMING(user);
        return false;
    }
    else if (bytesRead == 0) {
        Game::handleDisconnect_STATUS_OVER_CONFIRMING(user);
        return false;
    }

    if (!Game::handleBufferData_STATUS_OVER_CONFIRMING(buffer, user))
        return false;

    return true;

}

//STATUS_SELECT_GAME_DIFFICULTY


bool Game::process_STATUS_SELECT_GAME_DIFFICULTY(shared_ptr<User>& user)
{
    int key = Game::select_game_difficulty(user);

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
void Game::processBlockDown(shared_ptr<User>& user)
{
    if (Game::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (Game::blockDefines[user->getShape()][user->getForm()].space[i][j] == 1)
                {
                    user->setData(user->getRow() + i - 1, user->getCol() + j - 1, 1);

                    user->setColor(user->getRow() + i - 1, user->getCol() + j - 1, user->getShape());
                }
            }
        }



        user->setLine(0);

        // 消行处理，标记状态变化
        while (1)
        {
            int result = Game::Is_Increase_Score(user);
            if (result == 1) {
                continue;
            }
            else if (result == 0) {
                break;
            }
            else if (result == -1) {
                return;
            }
        }


        if (!Game::UpdateCurrentScore(user))
        {
            return;
        }

        if (!Game::IsOver(user))//判断是否结束
        {

            user->setShape(user->getNextShape());
            user->setForm(user->getNextForm());

            if (!Game::DrawSpace(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->setNextShape(rand() % 7);
            user->setNextForm(rand() % 4);

            user->setRow(1);
            user->setCol(WINDOW_COL_COUNT / 2 - 1);

            if (!Game::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
            {
                return;
            }

            if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//将该方块显示在初始下落位置
            {
                return;
            }
        }
        else
        {
            Filedata::update_TopScore_RecentScoreAsync(user);  // ✅ 异步，立即返回

            if (!UImanage::showover(user))
                return;
        }
    }
    else
    {
        if (!Game::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }

        user->setRow(user->getRow() + 1);

        if (!Game::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }
    }
}

void Game::handleTimedUserLogic(shared_ptr<User>& user)
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

void Game::processTimerEvent(int timerfd, short events, void* arg)
{
    // 锁内遍历，避免复制整个 users map（原 getAllUsers 按值返回，10 万连接时开销巨大）
    User::forEachUser([](int fd, std::shared_ptr<User>& user) {
        if (user->getStatus() == STATUS_PLAYING) {
            handleTimedUserLogic(user);
        }
    });
}


