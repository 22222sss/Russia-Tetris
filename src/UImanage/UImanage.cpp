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

bool UImanage::show_Password_Too_Short_Error(int i, const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    // 文案与 PasswordPolicy::kMinLength 一致（8）
    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE,
                    "密码至少需要 8 个字符（不能全是空格），请重新输入密码："))
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

bool UImanage::show_Login_Throttled(int retryAfterSeconds, const shared_ptr<User>& user)
{
    const string message = "登录尝试过于频繁，请 " + to_string(retryAfterSeconds) + " 秒后再试：";
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, message))
        return false;

    // 保持在"输入密码"状态，客户端等够时间后可以直接重输
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
        return false;
    return true;
}

bool UImanage::show_Registration_Throttled(int retryAfterSeconds, const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    const string message = "注册过于频繁，请 " + to_string(retryAfterSeconds) + " 秒后再试。";
    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, message))
        return false;

    // 额度是按 IP/全局算的，等一会儿再试比原地重输密码自然：回到"注册/登录"这一层
    // （调用方会切到 STATUS_REGISTER_OR_LOAD_OVER，按 3 回上级菜单）
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回上级菜单："))
        return false;
    return true;
}

bool UImanage::show_Server_Busy(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "服务繁忙，请稍后重试："))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
        return false;
    return true;
}

// ---------------- 修改密码（登录后，主菜单 5） ----------------

bool UImanage::show_Change_Password_Old(const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "修改密码"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入当前密码："))
        return false;
    return true;
}

bool UImanage::show_Receive_New_Password(const shared_ptr<User>& user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE,
                    "请输入新密码（至少 8 个字符，不能全是空格）："))
        return false;
    return true;
}

bool UImanage::show_Password_Unchanged_Error(const shared_ptr<User>& user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE,
                    "新密码不能与当前密码相同，请重新输入新密码："))
        return false;
    return true;
}

bool UImanage::show_Password_Changed(const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "密码修改成功！"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "下次登录请使用新密码。"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回主菜单："))
        return false;
    return true;
}

bool UImanage::show_Change_Password_Error(const string& message, const shared_ptr<User>& user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, message))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回主菜单："))
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
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 10, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "5. 修改密码"))
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
            // 回调已由 EventLoop::post 投递到事件循环线程执行；
            // 但异步期间连接可能已断开或状态已变，那时不能再渲染。
            if (user->getStatus() != STATUS_LOGIN_OVER) {
                return;
            }

            if (!success) {
                logger->error("读取最近成绩失败");
                // 这一屏之后状态仍是 STATUS_LOGIN_OVER（服务端只认"3"），所以每个出口
                // 都必须给出返回提示，否则玩家面对的是一屏没有任何按键说明的文字（6.56）
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "读取失败，请重试");
                outputText(user, WINDOW_ROW_COUNT / 3 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单：");
                return;
            }

            if (result.empty() || (result.size() == 1 && result.front() == "1")) {
                // Read_recent_grades 用单个 "1" 作为"用户存在但没有成绩"的哨兵值
                // （见 Filedata::Read_recent_grades），不能当成一条成绩渲染出来
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "暂无成绩记录");
                outputText(user, WINDOW_ROW_COUNT / 3 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单：");
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

            // 显示返回提示：紧跟在最后一条成绩下面。原来的 WINDOW_ROW_COUNT/2+21 = 33
            // 已经超出 24 行窗口，只能指望终端把光标钳到最后一行，等于没提示（6.56）
            outputText(user, WINDOW_ROW_COUNT / 3 + i + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单：");
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
            // 回调已由 EventLoop::post 投递到事件循环线程执行；
            // 但异步期间连接可能已断开或状态已变，那时不能再渲染。
            if (user->getStatus() != STATUS_LOGIN_OVER) {
                return;
            }

            if (!success) {
                logger->error("读取排行榜失败");
                outputText(user, WINDOW_ROW_COUNT / 3, 2 * (WINDOW_COL_COUNT / 3),
                    COLOR_WHITE, "加载失败，请重试");
                // 同 showRecentScores：失败出口也要给返回提示，否则这一屏没法退出去（6.56）
                outputText(user, WINDOW_ROW_COUNT / 3 + 2, 2 * (WINDOW_COL_COUNT / 3),
                    COLOR_WHITE, "请按3返回菜单：");
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

            // 三列最多占 WINDOW_ROW_COUNT/3(8) + 3 + 10 = 21 行，提示放最后一行
            // （原来的 WINDOW_ROW_COUNT/2+30 = 42 根本不在窗口里，见 6.56）
            outputText(user, WINDOW_ROW_COUNT - 1, 2 * (WINDOW_COL_COUNT / 3),
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
    // 用 ANSI 清屏码一次性清屏，替代 240 行空格（约 33KB → 7 字节）
    // \x1b[2J 清除整个屏幕，\x1b[H 光标回到左上角
    //
    // 前面那句 \x1b[?7l 是关掉"自动折行"(DECAWM)：棋盘固定占 60 列，窗口比它窄时，
    // 终端会把超出的字符折到下一行行首 —— 屏幕上表现为最左边多出一列彩色方块、
    // 文字被挤掉几个字符（2026-11-06 反馈）。关掉之后超宽部分直接截断，不再"跑到左边"。
    // 代价只有 4 字节，比任何重绘方案都便宜。
    //
    // \x1b[?25h 是"显示光标"：菜单/输入界面需要一个可见的插入符；游戏进行中会再
    // 调 hideCursor 藏起来（否则光标会停在棋盘上闪烁，2026-11-06 反馈）。
    return output(user, "\x1b[?25h\x1b[?7l\x1b[2J\x1b[H");
}

bool UImanage::hideCursor(const shared_ptr<User>& user)
{
    return output(user, "\x1b[?25l");
}