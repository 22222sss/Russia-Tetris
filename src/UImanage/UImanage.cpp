#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

UImanage::UImanage(){}

bool UImanage::showInitMenu(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 注册帐号"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. 登录"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    return true;
}

bool UImanage::show_Receive_Username(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入用户名："))
        return false;
    return true;
}

bool UImanage::show_Error_Message(int i,User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;
    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
        return false;
    return true;
}

bool UImanage::show_Username_Empty_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "用户名不可为空，请重新输入用户名："))
        return false;
    return true;
}

bool UImanage::show_Username_Taken_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "该用户名已被注册，请选择其他用户名:"))
        return false;
    return true;
}

bool UImanage::show_Receive_Password(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
        return false;
    return true;
}

bool UImanage::show_Password_Empty_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "密码不可为空，请重新输入密码："))
        return false;
    return true;
}

bool UImanage::show_Register_Success(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "注册成功！"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回上级菜单："))
        return false;
    return true;
}

bool UImanage::show_Login_Failure(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "登录失败，用户名或密码错误。"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "按3返回上级菜单:"))
        return false;
    return true;
}

bool UImanage::showLoadMenu(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 我最近的成绩"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. 全服top成绩"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. 开始游戏"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 8, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. 返回服务"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 12, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "你的选择是："))
        return false;
    return true;
}

bool UImanage::showRecentScores(User* user)
{

    int i = 0;

    if (!UImanage::clear(user))
        return false;

    vector<string> temp = Filedata::Read_recent_grades(user);

    if (temp.empty() || temp.front() == "-1")
    {
        return false;
    }
    else if (temp.front() != "1")
    {
        for (auto outcome : temp)
        {
            if (!outputText(user, WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, outcome))
                return false;

            i++;
        }
    }
    else if (temp.front() == "1")
    {
        if (!outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "No recent scores found")) {
            return false;
        }
    }


    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 21, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单："))
        return false;
    return true;
}

bool UImanage::showTopScores(User* user)
{
    
    if (!Filedata::loadPlayerData())
        return false;

    ifstream file("userdata.csv");//相对路径是相对于你的可执行文件所在的目录的

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();

        return false;
    }

    vector<PlayerInfo*> show = Filedata::Read_AllpalyerInfo(file);

    if (!UImanage::clear(user))
        return false;

    sort(show.begin(), show.end(), cmp_easy);

    UImanage::showTopScores_Easy(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT / 3, show, user);

    sort(show.begin(), show.end(), cmp_normal);

    UImanage::showTopScores_Normal(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 20, show, user);

    sort(show.begin(), show.end(), cmp_diffcult);

    UImanage::showTopScores_Diffcult(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 54, show, user);

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 40, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单："))
        return false;
    return true;
}

bool UImanage::showTopScores_Easy(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "简单模式："))
        return false;

    i += 2;

    if (!outputText(user, row + i, col, COLOR_WHITE, "用户名 分数 用户获取最高分的时间"))
        return false;

    i++;

    j = i;

    if (show.size() <= 10)
    {
        for (auto& player : show)
        {
            if (player->getMaximum_Score_Easy() > 0)
            {
                string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Easy()) + " " + player->getTimestampEasy();

                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;

                i++;
            }
        }
    }
    else
    {
        for (auto& player : show)
        {
            string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Easy()) + " " + player->getTimestampEasy();

            if (player->getMaximum_Score_Easy() == 0)
            {
                break;
            }

            if (player->getMaximum_Score_Easy() > 0)
            {
                i++;

                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;
            }

            if (i == j + 10)
            {
                break;
            }
        }
    }
    return true;
}

bool UImanage::showTopScores_Normal(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "普通模式："))
        return false;

    i += 2; //i=16

    if (!outputText(user, row + i, col, COLOR_WHITE, "用户名 分数 用户获取最高分的时间"))
        return false;

    i += 2;

    j = i;

    if (show.size() <= 10)
    {
        for (auto& player : show)
        {
            if (player->getMaximum_Score_Normal() > 0)
            {
                string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Normal()) + " " + player->getTimestampNormal();

                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;

                i++;
            }
        }
    }
    else
    {
        for (auto& player : show)
        {
            string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Normal()) + " " + player->getTimestampNormal();

            if (player->getMaximum_Score_Normal() == 0)
            {
                break;
            }

            if (player->getMaximum_Score_Normal() > 0)
            {
                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;

                i++;
            }

            if (i == j + 10)
            {
                break;
            }
        }
    }
    return true;
}

bool UImanage::showTopScores_Diffcult(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "困难模式："))
        return false;

    i += 2; //i=28

    if (!outputText(user, row + i, col, COLOR_WHITE, "用户名 分数 用户获取最高分的时间"))
        return false;

    i += 2;

    j = i;

    if (show.size() <= 10)
    {
        for (auto& player : show)
        {
            if (player->getMaximum_Score_Difficult() > 0)
            {
                string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Difficult()) + " " + player->getTimestampDifficult();

                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;

                i++;
            }
        }
    }
    else
    {
        for (auto& player : show)
        {
            string buffer = player->getPlayerName() + " " + to_string(player->getMaximum_Score_Difficult()) + " " + player->getTimestampDifficult();

            if (player->getMaximum_Score_Difficult() == 0)
            {
                break;
            }

            if (player->getMaximum_Score_Difficult() > 0)
            {
                if (!outputText(user, row + i, col, COLOR_WHITE, buffer))
                    return false;

                i++;
            }

            if (i == j + 10)
            {
                break;
            }
        }
    }
    return true;
}

bool UImanage::showGameDifficulty(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 简单模式"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. 普通模式"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. 困难模式"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. 返回上级菜单"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 8, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    return true;
}

bool UImanage::InitInterface(User* user)
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

bool UImanage::InitGameFace(User* user)
{

    if (!UImanage::clear(user))
        return false;

    if (!UImanage::InitInterface(user))//初始化界面
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

    if (!UImanage::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
    {
        return false;
    }

    if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //将该方块显示在初始下落位置
    {
        return false;
    }
    return true;
}

bool UImanage::DrawBlock(User* user, int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, Color(shape), "■"))
                    return false;
            }
        }
    }
    return true;
}

bool UImanage::DrawSpace(User* user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

bool UImanage::showover(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

bool UImanage::clear(User* user)
{
    int i;
    string emptyLine(6 * WINDOW_COL_COUNT, ' ');
    for (i = 1; i <= WINDOW_ROW_COUNT * 10; i++)
    {
        if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
            return false;
    }
    return true;
}