#include"User.h"
#include"Game.h"
#include"Common.h"
#include"Filedata.h"
#include"PlayerInfo.h"
#include"PasswordPolicy.h"
#include"AuthThrottle.h"
#include"IpThrottle.h"
#include"RegThrottle.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../EventLoop/EventLoop.h"
#include"../ConnectionPool/ConnectionPool.h"

extern shared_ptr<spdlog::logger> logger;

int Server::returnToInitMenu(shared_ptr<User>& user)
{
    
    int temp = Server::ReceiveData(user);
    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() == "3")
        {
            if (!UImanage::showInitMenu(user))
                return -1;
            return 1;
        }
        else
        {
            if(!UImanage::show_Error_Message(WINDOW_ROW_COUNT / 2 + 6,user))
                return -1;
            user->setReceivedata("");
        }
    }
    return 2;
}

int Server::ReceiveData(shared_ptr<User>& user)
{
    string endMarker = "\r\n";

    char buffer[1024] = { '\0' };
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer) - 1, 0);
    if (bytesRead == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 没有可用数据，继续等待
            return 0;
        }

        user->setStatus(STATUS_OVER_QUIT);
        close(user->getFd());
        //printf("Client[%d] recv Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
        logger->flush();
        return -1;
    }
    else if (bytesRead == 0)
    {
        // 客户端连接已关闭
        //printf("Client[%d] disconnect!\n", this->fd);
        logger->info("Client[{}] disconnect!\n", user->getFd());
        logger->flush();

        user->setStatus(STATUS_OVER_QUIT);
        close(user->getFd());
        return -1;
    }

    //user->receivedata += buffer;

    user->setReceivedata(user->getReceivedata() + buffer);

    size_t endPos = user->getReceivedata().find(endMarker);
    if (endPos != string::npos)
    {
        user->setReceivedata(user->getReceivedata().substr(0, endPos));
        
        return 1;
    }
    return 2;
}

int Server::returnToLoadMenu(shared_ptr<User>& user, int i)
{
    i++;

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() == "3")
        {
            if (!UImanage::showLoadMenu(user))
                return -1;
            return 1;
        }
        else
        {
            user->setReceivedata(""); 

            if (!UImanage::show_Error_Message(WINDOW_ROW_COUNT / 2 + i, user))
                return -1;
        }
    }

    return 2;
}

int Server::receive_username_register(shared_ptr<User>& user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    string username = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            username = user->getReceivedata();
            user->setReceivedata(""); 
        }
        else
        {
            if (!UImanage::show_Username_Empty_Error(WINDOW_ROW_COUNT / 2 + 1,user))
            {
                return -1;
            }
        }
    }

    if (username != "")
    {
        if (isUserExists(username))
        {
            if (!UImanage::show_Username_Taken_Error(WINDOW_ROW_COUNT / 2 + 1, user))
            {
                return -1;
            }
            user->setReceivedata("");
            return 0;
        }
        else
        {
            user->setUsername(username);

            if (!UImanage::show_Receive_Password(user))
            {
                return -1;
            }

            return 1;
        }
    }

    return 2;
}

int Server::receive_password_register(shared_ptr<User>& user)
{
    if (!Filedata::loadPlayerData())
        return false;

    string password = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            password = user->getReceivedata();
            user->setReceivedata(""); 
        }
        else
        {
            if (!UImanage::show_Password_Empty_Error(WINDOW_ROW_COUNT / 2 + 3,user))
            {
                return -1;
            }
            user->setReceivedata("");
        }
    }

    if (password != "")
    {
        // 注册限流（HANDOVER 6.30）：注册是唯一"免费的 PBKDF2 入口"，而且它的哈希
        // 跑在唯一的异步写线程上（和登录查询/成绩写入同一条队列）。所以在**提交
        // 哈希之前**按 IP + 全局两道滑动窗口挡掉；被拒绝的尝试不记账（拒绝本身不花
        // 代价，记账只会把"等一会儿再来"的人越锁越久）。
        const auto regNow = std::chrono::steady_clock::now();
        const auto regDecision = RegThrottle::check(user->getPeerIp(), regNow);
        if (regDecision.throttled)
        {
            user->setReceivedata("");
            logger->warn("注册过于频繁（{}），来源 IP {}，{} 秒后可重试",
                         regDecision.global ? "全局额度" : "单 IP 额度",
                         IpThrottle::format(user->getPeerIp()),
                         regDecision.retryAfterSeconds);
            if (!UImanage::show_Registration_Throttled(regDecision.retryAfterSeconds, user))
            {
                return -1;
            }
            return 1;   // 由 process_STATUS_RECEIVE_PASSWORD_REGISTER 切到"按3返回"
        }

        // 注册密码策略：至少 8 个字符、不能全是空白（见 PasswordPolicy.h）。
        // 只拦注册；老账号登录不检查，避免升级后被自己的短密码锁在门外。
        if (!PasswordPolicy::check(password))
        {
            user->setReceivedata("");
            if (!UImanage::show_Password_Too_Short_Error(WINDOW_ROW_COUNT / 2 + 3, user))
            {
                return -1;
            }
            return 0;   // 留在"输入密码"状态，客户端可重输
        }

        user->setPassword(password);

        // 记账放在"确定要算哈希"的位置：策略不通过/被拒绝的尝试都不占额度
        RegThrottle::onAttempt(user->getPeerIp(), regNow);

        Filedata::saveNewUserDataAsync(user);  // ✅ 异步调用，立即返回

        if (!UImanage::show_Register_Success(user))
            return -1;

        return 1;
    }
    return 0;
}

int Server::receive_username_load(shared_ptr<User>& user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    string username = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            username = user->getReceivedata();
            user->setReceivedata("");
        }
        else
        {
            if (!UImanage::show_Username_Empty_Error(WINDOW_ROW_COUNT / 2 + 1, user))
            {
                return -1;
            }
        }
    }

    if (username != "")
    {
        user->setUsername(username);

        if (!UImanage::show_Receive_Password(user))
        {
            return -1;
        }

        return 1;
    }

    return 2;
}

int Server::receive_password_load(shared_ptr<User>& user)
{
    string password = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        //delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            password = user->getReceivedata();
            user->setReceivedata("");
        }
        else
        {
            if (!UImanage::show_Password_Empty_Error(WINDOW_ROW_COUNT / 2 + 3,user))
            {
                return -1;
            }
            user->setReceivedata("");
        }
    }

    if (password != "")
    {
        const auto now = std::chrono::steady_clock::now();

        // 按来源 IP 的粗粒度兜底（见 TetrisGame/IpThrottle.h）。
        // 用户名退避挡不住"换用户名喷洒"：每个新用户名都有 5 次免延迟，且攻击者
        // 可以用不存在的用户名，每次都逼服务端跑一次假 PBKDF2。这一层按 IP 封顶。
        // 被拒绝的尝试不记账、也不延长窗口，否则同 NAT 后面的人会被一起锁死。
        const auto ipDecision = IpThrottle::check(user->getPeerIp(), now);
        if (ipDecision.throttled)
        {
            user->setReceivedata("");
            logger->warn("来源 IP {} 登录失败过多，拒绝本次尝试（{} 秒后可重试）",
                         IpThrottle::format(user->getPeerIp()),
                         ipDecision.retryAfterSeconds);
            if (!UImanage::show_Login_Throttled(ipDecision.retryAfterSeconds, user))
            {
                return -1;
            }
            return 2;   // 保持"输入密码"状态，客户端等够时间后可重试
        }

        // 失败退避：同一用户名连续失败太多次时，本次直接拒绝（不跑 PBKDF2）。
        // 窗口内的尝试只被拒绝、不延长窗口——否则会被拿来把别人的账号锁死。
        const auto throttle = AuthThrottle::check(user->getUsername(), now);
        if (throttle.throttled)
        {
            user->setReceivedata("");
            if (!UImanage::show_Login_Throttled(throttle.retryAfterSeconds, user))
            {
                return -1;
            }
            return 2;   // 保持"输入密码"状态，客户端按提示等待后可重试
        }

        user->setPassword(password);

        // PBKDF2 是有意设计得慢的（当前约 50ms/次）。如果在事件循环线程上算，
        // 一个登录就会卡住所有连接（包括正在游戏的玩家），所以交给异步线程。
        user->setStatus(STATUS_VERIFYING_PASSWORD);

        const string loginUsername = user->getUsername();
        const std::uint32_t peerIp = user->getPeerIp();   // 异步回调里不再回头读 User
        Filedata::loginAsync(loginUsername, password,
            [user, loginUsername, peerIp](Filedata::LoginResult result)
            {
                // 校验期间连接可能已断开（甚至 fd 已被回收复用），
                // 状态一变就不能再往"这条连接"上渲染。与其它异步回调同一约定。
                if (user->getStatus() != STATUS_VERIFYING_PASSWORD)
                {
                    logger->warn("密码校验完成时状态已变化（{}），丢弃本次结果",
                                 static_cast<int>(user->getStatus()));
                    return;
                }

                if (result == Filedata::LoginResult::Busy)
                {
                    // 本次根本没校验：不能算失败（否则"系统繁忙"会污染退避计数）
                    user->setStatus(STATUS_RECEIVE_PASSWORD_LOAD);
                    user->setReceivedata("");
                    UImanage::show_Server_Busy(user);
                    logger->warn("登录被限流（哈希队列已满）: {}", loginUsername);
                    return;
                }

                if (result == Filedata::LoginResult::BadCredentials)
                {
                    const auto failedAt = std::chrono::steady_clock::now();
                    AuthThrottle::onFailure(loginUsername, failedAt);
                    // IP 维度同时记一笔：换用户名喷洒时只有这一层会生效
                    IpThrottle::onFailure(peerIp, failedAt);
                    UImanage::show_Login_Failure(user);
                    user->setStatus(STATUS_REGISTER_OR_LOAD_OVER);
                    user->setReceivedata("");
                    logger->info("用户登录失败: {}", loginUsername);
                    return;
                }

                AuthThrottle::onSuccess(loginUsername);

                // loginAsync 已经重新加载过账号表（校验就是用最新的那份），
                // 这里直接进主菜单；选难度时还会再刷新一次成绩数据。
                UImanage::showLoadMenu(user);
                user->setStatus(STATUS_LOGIN);
                user->setReceivedata("");
                logger->info("用户登录成功: {}", loginUsername);
            });

        return 1;
    }

    return 2;
}

int Server::receive_changepassword_old(shared_ptr<User>& user)
{
    string password = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            password = user->getReceivedata();
            user->setReceivedata("");
        }
        else
        {
            if (!UImanage::show_Password_Empty_Error(WINDOW_ROW_COUNT / 2 + 2, user))
            {
                return -1;
            }
            user->setReceivedata("");
        }
    }

    if (password != "")
    {
        // 暂存在 password 字段里：新密码收齐后一起交给 changePasswordAsync。
        // 不单独加字段是因为 User 有 2KB 预算（现在 1992B），一个 string 就是 32B。
        user->setPassword(password);

        if (!UImanage::show_Receive_New_Password(user))
        {
            return -1;
        }
        return 1;
    }

    return 2;
}

int Server::receive_changepassword_new(shared_ptr<User>& user)
{
    string newPassword = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        return -1;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() != "")
        {
            newPassword = user->getReceivedata();
            user->setReceivedata("");
        }
        else
        {
            if (!UImanage::show_Password_Empty_Error(WINDOW_ROW_COUNT / 2 + 4, user))
            {
                return -1;
            }
            user->setReceivedata("");
        }
    }

    if (newPassword == "")
    {
        return 2;
    }

    // 新密码与注册走同一套策略；不满足时留在"输入新密码"，可以重输
    if (!PasswordPolicy::check(newPassword))
    {
        user->setReceivedata("");
        if (!UImanage::show_Password_Too_Short_Error(WINDOW_ROW_COUNT / 2 + 2, user))
        {
            return -1;
        }
        return 0;
    }

    // 和当前密码相同就没必要改：既省一次写盘，也避免用户以为改了
    if (newPassword == user->getPassword())
    {
        user->setReceivedata("");
        if (!UImanage::show_Password_Unchanged_Error(user))
        {
            return -1;
        }
        return 0;
    }

    // 改密也是一次凭据校验：不接退避的话，它就是绕过登录限流猜密码的旁路
    const auto now = std::chrono::steady_clock::now();
    const auto ipDecision = IpThrottle::check(user->getPeerIp(), now);
    int retryAfterSeconds = 0;
    if (ipDecision.throttled)
    {
        retryAfterSeconds = ipDecision.retryAfterSeconds;
    }
    else
    {
        const auto throttle = AuthThrottle::check(user->getUsername(), now);
        if (throttle.throttled)
        {
            retryAfterSeconds = throttle.retryAfterSeconds;
        }
    }
    if (retryAfterSeconds > 0)
    {
        user->setPassword("");
        user->setReceivedata("");
        if (!UImanage::show_Change_Password_Error(
                "修改密码尝试过于频繁，请 " + to_string(retryAfterSeconds) + " 秒后再试：", user))
        {
            return -1;
        }
        return 3;   // 回主菜单（见 process_STATUS_CHANGE_PASSWORD_NEW）
    }

    const string username = user->getUsername();
    const string oldPassword = user->getPassword();
    const std::uint32_t peerIp = user->getPeerIp();

    user->setStatus(STATUS_VERIFYING_PASSWORD);

    Filedata::changePasswordAsync(username, oldPassword, newPassword,
        [user, username, peerIp](Filedata::PasswordChangeResult result)
        {
            // 校验期间连接可能已断开（甚至对象已被池复用），状态一变就不能再渲染
            if (user->getStatus() != STATUS_VERIFYING_PASSWORD)
            {
                logger->warn("改密回调到达时状态已变化（{}），丢弃本次结果",
                             static_cast<int>(user->getStatus()));
                return;
            }

            // 无论结果如何都别把明文密码留在连接对象里（对象会回池复用）
            user->setPassword("");
            user->setReceivedata("");

            switch (result)
            {
            case Filedata::PasswordChangeResult::Ok:
                AuthThrottle::onSuccess(username);
                UImanage::show_Password_Changed(user);
                user->setStatus(STATUS_LOGIN_OVER);   // 按 3 回主菜单
                logger->info("用户修改密码成功: {}", username);
                break;

            case Filedata::PasswordChangeResult::BadCredentials:
            {
                const auto failedAt = std::chrono::steady_clock::now();
                AuthThrottle::onFailure(username, failedAt);
                IpThrottle::onFailure(peerIp, failedAt);
                UImanage::show_Change_Password_Error("当前密码错误，密码未修改。", user);
                user->setStatus(STATUS_LOGIN_OVER);
                logger->info("用户修改密码失败（当前密码错误）: {}", username);
                break;
            }

            case Filedata::PasswordChangeResult::Busy:
                // 根本没校验，不能计失败
                UImanage::show_Change_Password_Error("服务繁忙，密码未修改，请稍后重试。", user);
                user->setStatus(STATUS_LOGIN_OVER);
                logger->warn("修改密码被限流（哈希队列已满）: {}", username);
                break;

            case Filedata::PasswordChangeResult::Error:
                UImanage::show_Change_Password_Error("修改失败（服务端暂时无法写盘），密码未修改。", user);
                user->setStatus(STATUS_LOGIN_OVER);
                logger->error("修改密码失败（存储错误）: {}", username);
                break;
            }
        });

    return 1;
}

int Server::loginUser(shared_ptr<User>& user)
{
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
            if (!UImanage::showRecentScores(user))
            {
                return -1;
            }

            return 1;
        }
        else if (user->getReceivedata() == "2")
        {
            if (!UImanage::showTopScores(user))
            {
                return -1;
            }

            return 2;
        }
        else if (user->getReceivedata() == "3")
        {
            if (!UImanage::showGameDifficulty(user))
            {
                return -1;
            }

            return 3;
        }
        else if (user->getReceivedata() == "4")
        {
            if (!UImanage::showInitMenu(user))
            {
                return -1;
            }

            return 4;
        }
        else if (user->getReceivedata() == "5")
        {
            // 修改密码（只在登录后有意义）。返回 6 而不是 1~4，避开上面的"输入错误"分支。
            if (!UImanage::show_Change_Password_Old(user))
            {
                return -1;
            }

            return 6;
        }
        else
        {
           
            if (!UImanage::show_Error_Message(WINDOW_ROW_COUNT / 2 + 14, user))
                return -1;

            user->setReceivedata("");
        }
    }
    return 5;
}

bool Server::process_STATUS_NOTSTART(shared_ptr<User>& user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    int temp = Server::ReceiveData(user);
    if (temp == -1)
    {
        //delete user;
        return false;
    }
    else if (temp == 1)
    {
        if (user->getReceivedata() == "1")
        {
            //注册
            if (!UImanage::show_Receive_Username(user))
                return false;
            user->setStatus(STATUS_RECEIVE_USERNAME_REGISTER);
            user->setReceivedata("");
        }
        else if (user->getReceivedata() == "2")
        {
            //登录
            if (!UImanage::show_Receive_Username(user))
                return false;
            user->setStatus(STATUS_RECEIVE_USERNAME_LOAD);
            user->setReceivedata("");
        }
        else
        {
            user->setReceivedata("");
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 1, COLOR_WHITE, emptyLine))
                return false;
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
                return false;
        }
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_USERNAME_REGISTER(shared_ptr<User>& user)
{
    //std::unique_ptr<Filedata> filedata(new Filedata);

    if (!Filedata::loadPlayerData())
    {
        return false;
    }

    int key = Server::receive_username_register(user);

    if (key == -1) {
        return false;
    }
    else if (key == 1) {
        user->setStatus(STATUS_RECEIVE_PASSWORD_REGISTER); 
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_PASSWORD_REGISTER(shared_ptr<User>& user)
{
    int key = Server::receive_password_register(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setStatus(STATUS_REGISTER_OR_LOAD_OVER);
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_USERNAME_LOAD(shared_ptr<User>& user)
{
    int key = Server::receive_username_load(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setStatus(STATUS_RECEIVE_PASSWORD_LOAD);
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_PASSWORD_LOAD(shared_ptr<User>& user)
{
    int key = Server::receive_password_load(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        // 密码校验已交给异步线程；这里只给用户一个即时提示，
        // 主菜单/登录失败由 loginAsync 的回调负责渲染（见 receive_password_load）。
        outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3),COLOR_WHITE, "登录中...");
    }
    return true;
}

bool Server::process_STATUS_LOGIN(shared_ptr<User>& user)
{
    int key = Server::loginUser(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_LOGIN_OVER);
    }
    else if (key == 2)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_LOGIN_OVER);
    }
    else if (key == 3)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_SELECT_GAME_DIFFICULTY);
    }
    else if (key == 4)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_NOTSTART);
    }
    else if (key == 6)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_CHANGE_PASSWORD_OLD);
    }
    return true;
}

bool Server::process_STATUS_LOGIN_OVER(shared_ptr<User>& user)
{
    int key = Server::returnToLoadMenu(user, WINDOW_ROW_COUNT / 3 + 20);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setStatus(STATUS_LOGIN);
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_REGISTER_OR_LOAD_OVER(shared_ptr<User>& user)
{
    int key = Server::returnToInitMenu(user);
    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setStatus(STATUS_NOTSTART);
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_CHANGE_PASSWORD_OLD(shared_ptr<User>& user)
{
    int key = Server::receive_changepassword_old(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setStatus(STATUS_CHANGE_PASSWORD_NEW);
    }
    return true;
}

bool Server::process_STATUS_CHANGE_PASSWORD_NEW(shared_ptr<User>& user)
{
    int key = Server::receive_changepassword_new(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        // 校验/落盘已交给异步线程，主菜单/失败提示由回调渲染
        outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "正在修改密码...");
    }
    else if (key == 3)
    {
        // 被退避拒绝：提示里已经让用户"按 3 返回主菜单"
        user->setStatus(STATUS_LOGIN_OVER);
    }
    return true;
}


// ✅ 使用连接池的方式
void Server::handleNewClientConnection(int serverSocket, short events, void* arg) 
{
    // 记录来源 IP（按 IP 兜底的输入，见 TetrisGame/IpThrottle.h）。
    // 监听 socket 是 AF_INET，所以正常情况就是 sockaddr_in；不是 IPv4 时存 0，
    // IpThrottle 会跳过限流（宁可漏记，也不要因为拿不到地址去误伤）。
    struct sockaddr_storage peerAddr;
    memset(&peerAddr, 0, sizeof(peerAddr));
    socklen_t peerLen = sizeof(peerAddr);
    int clientSocket = accept(serverSocket,
                              reinterpret_cast<struct sockaddr*>(&peerAddr), &peerLen);
    if (clientSocket == -1) {
        logger->error("accept失败: {}", strerror(errno));
        return;
    }

    std::uint32_t peerIp = 0;
    if (peerAddr.ss_family == AF_INET) {
        peerIp = ntohl(reinterpret_cast<const struct sockaddr_in*>(&peerAddr)->sin_addr.s_addr);
    }

    logger->info("Client[{}] 连接! 来自 {}", clientSocket, IpThrottle::format(peerIp));


    // 🎯 关键改变：从连接池获取，而不是new
    auto newUser = ConnectionPool::acquire(clientSocket);

    if (!newUser) {
        close(clientSocket);
        logger->error("从连接池获取User对象失败: {}", clientSocket);
        return;
    }

    // 记在这条连接上：登录失败时按它计数（连接池复用时 resetUserInfo 会清掉）
    newUser->setPeerIp(peerIp);

    // 注意：这里使用newUser.get()获取原始指针，保持原有接口兼容
    User::addUser(clientSocket, newUser);

    EventLoop* eventloop = static_cast<EventLoop*>(arg);
    eventloop->registerFdEvent(clientSocket, EV_READ | EV_PERSIST,
        Server::handleClientData, nullptr);

    if (!IsSetSocketBlocking(clientSocket, false)) {
        // 必须先从 users 移除再归还连接池：否则 users 里会留下一条
        // 指向"池中对象"的条目（其 fd 已被 release 改成 -1），
        // 该对象随后被新连接复用时，同一个对象就同时挂在两个 fd 上。
        User::removeUser(clientSocket);
        EventLoop::removeFdEvent(clientSocket);
        ConnectionPool::release(newUser);  // 失败时立即归还
        return;
    }

    if (!UImanage::showInitMenu(newUser)) {
        User::removeUser(clientSocket);
        EventLoop::removeFdEvent(clientSocket);
        ConnectionPool::release(newUser);  // 失败时立即归还
        return;
    }
}

void Server::handleClientData(int clientSocket, short events, void* arg)
{

    // 通过clientSocket从User::getUsers()中获取shared_ptr
    auto user = User::getUser(clientSocket);
    if (!user) {
        logger->error("handleClientData: 找不到对应的用户, fd: {}", clientSocket);
        return;
    }

    // 一次事件处理里可能有十几次 outputText（一次按键的重绘），
    // 先全部攒进发送缓冲，函数结束时合并成一次 send，减少 syscall。
    OutputBatch batch(user);

    // 处理已连接客户端的数据接收事件
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!Game::process_STATUS_PLAYING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_OVER_CONFIRMING)
    {
        if (!Game::process_STATUS_OVER_CONFIRMING(user))
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
    else if (user->getStatus() == STATUS_VERIFYING_PASSWORD)
    {
        // 异步校验期间客户端若又发了数据，读走丢弃：
        // libevent 默认水平触发，不读会让这条连接被反复唤醒空转。
        int temp = Server::ReceiveData(user);
        if (temp == -1)
        {
            return;
        }
        user->setReceivedata("");
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
        if (!Game::process_STATUS_SELECT_GAME_DIFFICULTY(user))
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
    else if (user->getStatus() == STATUS_CHANGE_PASSWORD_OLD)
    {
        if (!Server::process_STATUS_CHANGE_PASSWORD_OLD(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_CHANGE_PASSWORD_NEW)
    {
        if (!Server::process_STATUS_CHANGE_PASSWORD_NEW(user))
        {
            return;
        }
    }
}