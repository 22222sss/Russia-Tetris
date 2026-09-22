#include"UImanage.h"

extern shared_ptr<spdlog::logger> logger;

bool UImanage::showInitMenu(const shared_ptr<User>& user)
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

bool UImanage::show_Receive_Username(const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入用户名："))
        return false;
    return true;
}

bool UImanage::show_Error_Message(int i, const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;
    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
        return false;
    return true;
}

bool UImanage::show_Username_Empty_Error(int i, const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "用户名不可为空，请重新输入用户名："))
        return false;
    return true;
}

bool UImanage::show_Username_Taken_Error(int i, const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "该用户名已被注册，请选择其他用户名:"))
        return false;
    return true;
}

bool UImanage::show_Receive_Password(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
        return false;
    return true;
}

bool UImanage::show_Password_Empty_Error(int i, const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "密码不可为空，请重新输入密码："))
        return false;
    return true;
}

bool UImanage::show_Register_Success(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "注册成功！"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回上级菜单："))
        return false;
    return true;
}

bool UImanage::show_Login_Failure(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "登录失败，用户名或密码错误。"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "按3返回上级菜单:"))
        return false;
    return true;
}

bool UImanage::showLoadMenu(const shared_ptr<User>& user)
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

bool UImanage::showRecentScores(const shared_ptr<User>& user)
{
   /* int i = 0;

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
    return true;*/

    if (!UImanage::clear(user))
        return false;

    // 显示"加载中..."提示
    if (!outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3),COLOR_WHITE, "加载中..."))
        return false;

    // 🔥 使用异步读取，传入callback处理结果
    Filedata::readRecentGradesAsync(user,
        [user](bool success, vector<string> result) {
            // 这个callback会在后台线程完成时自动调用

            if (!success) {
                logger->error("读取最近成绩失败");
                // 显示错误信息
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "读取失败，请重试");
                return;
            }

            if (result.empty()) {
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "暂无成绩记录");
                return;
            }

            // 清空"加载中..."提示
            string emptyLine(50, ' ');
            outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, emptyLine);

            // 显示实际成绩
            int i = 0;
            for (const auto& outcome : result) {
                outputText(user, WINDOW_ROW_COUNT / 3 + i,2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, outcome);
                i++;
            }

            // 显示返回提示
            outputText(user, WINDOW_ROW_COUNT / 2 + 21, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单：");
        }
    );

    return true;

}

bool UImanage::showTopScores(const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    // 显示"加载中..."提示
    if (!outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3),
        COLOR_WHITE, "加载排行榜中..."))
        return false;

    // 🔥 使用异步读取，传入callback处理结果
    Filedata::readAllPlayerInfoAsync(
        [user](bool success, vector<shared_ptr<PlayerInfo>> allPlayers) {
            if (!success) {
                logger->error("读取排行榜失败");
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3),
                    COLOR_WHITE, "加载失败，请重试");
                return;
            }

            // 清空"加载中..."提示
            string emptyLine(50, ' ');
            outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3),
                COLOR_WHITE, emptyLine);

            // 处理并显示排行榜数据
            vector<shared_ptr<PlayerInfo>> show = allPlayers;

            if (!UImanage::clear(user))
                return;

            sort(show.begin(), show.end(), cmp_easy);
            UImanage::showTopScores_Easy(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT / 3, show, user);

            sort(show.begin(), show.end(), cmp_normal);
            UImanage::showTopScores_Normal(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 20, show, user);

            sort(show.begin(), show.end(), cmp_diffcult);
            UImanage::showTopScores_Diffcult(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 54, show, user);

            outputText(user, WINDOW_ROW_COUNT / 2 + 30, 2 * (WINDOW_COL_COUNT / 3),
                COLOR_WHITE, "请按3返回菜单：");
        }
    );

    return true;
}

bool UImanage::showTopScores_Easy(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user)
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

bool UImanage::showTopScores_Normal(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user)
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

bool UImanage::showTopScores_Diffcult(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user)
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

bool UImanage::showGameDifficulty(const shared_ptr<User>& user)
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

bool UImanage::showover(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

bool UImanage::clear(const shared_ptr<User>& user)
{
    // 合并所有清屏输出为一次 send，避免 240 次循环 × 多次 send 的系统调用开销
    string buf;
    buf.reserve(WINDOW_ROW_COUNT * 10 * (6 * WINDOW_COL_COUNT + 32));
    string emptyLine(6 * WINDOW_COL_COUNT, ' ');
    for (int i = 1; i <= WINDOW_ROW_COUNT * 10; i++)
    {
        buf += "\x1b[" + to_string(i) + ";1H\33[" + to_string(COLOR_WHITE) + "m" + emptyLine;
    }
    return output(user, buf);
}