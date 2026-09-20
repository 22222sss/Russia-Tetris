#include"User.h"
#include"Game.h"
#include"Common.h"
#include"Filedata.h"
#include"PlayerInfo.h"
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
        user->setPassword(password);

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

    if (!Filedata::loadPlayerData())
    {
        return -1;
    }

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
        }
    }

    if (password != "")
    {
        user->setPassword(password);

        for (const auto& player : PlayerInfo::getPlayers())
        {
            if (player->getPlayerName() == user->getUsername() && player->getPassword() == user->getPassword())
            {
                return 1;
            }
        }

        if (!UImanage::show_Login_Failure(user))
        {
            return -1;
        }
        
        return 0;
    }

    return 2;
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
    //std::unique_ptr<UImanage> UI(new UImanage);

    int key = Server::receive_password_load(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        // 显示"登录中..."提示
        outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3),COLOR_WHITE, "登录中...");

        // 🔥 异步加载玩家数据，加载完成后显示菜单
        Filedata::loadPlayerDataAsync(
            [user](bool success) 
            {
                if (success) 
                {
                    // 加载成功，显示主菜单
                    UImanage::showLoadMenu(user);
                    user->setStatus(STATUS_LOGIN);
                    user->setReceivedata("");

                    logger->info("用户登录成功: {}", user->getUsername());
                }
                else 
                {
                    // 加载失败，显示错误
                    UImanage::show_Login_Failure(user);
                    user->setStatus(STATUS_REGISTER_OR_LOAD_OVER);
                    user->setReceivedata("");

                    logger->error("用户登录数据加载失败: {}", user->getUsername());
                }
            }
        );
    }
    else if (key == 0)
    {
        user->setStatus(STATUS_REGISTER_OR_LOAD_OVER);
        user->setReceivedata("");
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


// ✅ 使用连接池的方式
void Server::handleNewClientConnection(int serverSocket, short events, void* arg) 
{
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1) {
        logger->error("accept失败: {}", strerror(errno));
        return;
    }

    logger->info("Client[{}] 连接!", clientSocket);


    // 🎯 关键改变：从连接池获取，而不是new
    auto newUser = ConnectionPool::acquire(clientSocket);

    if (!newUser) {
        close(clientSocket);
        logger->error("从连接池获取User对象失败: {}", clientSocket);
        return;
    }

    // 注意：这里使用newUser.get()获取原始指针，保持原有接口兼容
    User::addUser(clientSocket, newUser);

    EventLoop* eventloop = static_cast<EventLoop*>(arg);
    eventloop->registerFdEvent(clientSocket, EV_READ | EV_PERSIST,
        Server::handleClientData, nullptr);

    if (!IsSetSocketBlocking(clientSocket, false)) {
        ConnectionPool::release(newUser);  // 失败时立即归还
        return;
    }

    if (!UImanage::showInitMenu(newUser)) {
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
}