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

int Server::returnToInitMenu(User* user)
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

int Server::ReceiveData(User* user)
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

int Server::returnToLoadMenu(User* user, int i)
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

int Server::receive_username_register(User* user)
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

int Server::receive_password_register(User* user)
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

        Filedata::saveNewUserData(user);

        if (!UImanage::show_Register_Success(user))
            return -1;

        return 1;
    }
    return 0;
}

int Server::receive_username_load(User* user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    string username = "";

    int temp = Server::ReceiveData(user);

    if (temp == -1)
    {
        delete user;
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

int Server::receive_password_load(User* user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    //std::unique_ptr<Filedata> filedata(new Filedata);

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

int Server::loginUser(User* user)
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

bool Server::process_STATUS_NOTSTART(User* user)
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

bool Server::process_STATUS_RECEIVE_USERNAME_REGISTER(User* user)
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

bool Server::process_STATUS_RECEIVE_PASSWORD_REGISTER(User* user)
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

bool Server::process_STATUS_RECEIVE_USERNAME_LOAD(User* user)
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

bool Server::process_STATUS_RECEIVE_PASSWORD_LOAD(User* user)
{
    //std::unique_ptr<UImanage> UI(new UImanage);

    int key = Server::receive_password_load(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        if (!UImanage::showLoadMenu(user))
            return false;
        user->setStatus(STATUS_LOGIN);
        user->setReceivedata("");
    }
    else if (key == 0)
    {
        user->setStatus(STATUS_REGISTER_OR_LOAD_OVER);
        user->setReceivedata("");
    }
    return true;
}

bool Server::process_STATUS_LOGIN(User* user)
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

bool Server::process_STATUS_LOGIN_OVER(User* user)
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

bool Server::process_STATUS_REGISTER_OR_LOAD_OVER(User* user)
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


void Server::handleNewClientConnection(int serverSocket, short events, void* arg)
{
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1)
    {
        //printf("accept Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        logger->error("accept Error: {} (errno: {}) In handleNewClientConnection\n", strerror(errno), errno);
        logger->flush();
        return;
    }
    else
    {
        //printf("Client[%d], welcome!\n", clientSocket);
        //Client.push_back(client);
        logger->info("Client[{}], welcome!\n", clientSocket);
        logger->flush();
    }

    // 创建新的用户信息结构体
    User* newUser = new User(clientSocket);

    if (newUser == nullptr)
    {
        close(clientSocket);
        //printf("allocate memory for newUser Error In handleNewClientConnection");
        logger->error("allocate memory for newUser Error In handleNewClientConnection\n");
        logger->flush();
        return;
    }

    User::getUsers().insert(make_pair(clientSocket, newUser));

    // 创建并添加客户端数据事件

    EventLoop* eventloop = static_cast<EventLoop*>(arg);

    eventloop->registerFdEvent(clientSocket, EV_READ | EV_PERSIST, Server::handleClientData, (void*)newUser);

    // 设置客户端连接为非阻塞模式
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    if (!UImanage::showInitMenu(newUser))
        return;
}

void Server::handleClientData(int clientSocket, short events, void* arg)
{
    User* user = (User*)arg;

    // 处理已连接客户端的数据接收事件
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!TetrisGame::process_STATUS_PLAYING(user))
        {
            return;
        }
    }
    else if (user->getStatus() == STATUS_OVER_CONFIRMING)
    {
        if (!TetrisGame::process_STATUS_OVER_CONFIRMING(user))
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
        if (!TetrisGame::process_STATUS_SELECT_GAME_DIFFICULTY(user))
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