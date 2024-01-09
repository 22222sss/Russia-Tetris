#include"Game.h"
#include"Public.h"
#include"UserInfo.h"
#include"Public_game.h"
#include"Player.h"
#include"EventLoop.h"
#include"Server.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, UserInfo*> g_playing_gamer;

extern shared_ptr<spdlog::logger> logger;

Server::Server(){}

bool Server::showInitMenu(UserInfo* user)
{
    if (!clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. ע���ʺ�"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. ��¼"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    return true;
}

bool Server::registerUser(UserInfo* user, int epollfd)
{
    if (!clear(user))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�������û�����"))
        return false;
    return true;
}

int Server::returnToInitMenu(UserInfo* user, int epollfd)
{
    int temp = this->ReceiveData(user,epollfd);
    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "3")
        {
            if (!this->showInitMenu(user))
                return -1;
            return 1;
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 6, 1, COLOR_WHITE, emptyLine))
                return -1;
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������:"))
                return -1;

            user->receivedata = "";
        }
    }
    return 2;
}

int Server::ReceiveData(UserInfo* user, int epollfd)
{
    string endMarker = "\r\n";

    char buffer[1024] = { '\0' };
    int bytesRead = recv(user->fd, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // û�п������ݣ������ȴ�
            return 0;
        }
        close(user->fd);
        //printf("Client[%d] recv Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        logger->error("Client[{}] recv Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
        logger->flush();
        return -1;
    }
    else if (bytesRead == 0)
    {
        // �ͻ��������ѹر�
        //printf("Client[%d] disconnect!\n", this->fd);
        logger->info("Client[{}] disconnect!\n", user->fd);
        logger->flush();
        if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, user->fd, nullptr)) {
            perror("epoll_ctl EPOLL_CTL_DEL");
            logger->error("epoll_ctl EPOLL_CTL_DEL: {}", strerror(errno));
            logger->flush();
        }
        close(user->fd);
        return -1;
    }

    user->receivedata += buffer;

    size_t endPos = user->receivedata.find(endMarker);
    if (endPos != string::npos)
    {
        user->receivedata = user->receivedata.substr(0, endPos);
        return 1;
    }
    return 2;
}

bool Server::loadUser(UserInfo* user, int epollfd)
{
    if (!clear(user))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�������û�����"))
        return false;
    return true;
}

void Server::saveUserData(UserInfo* user)
{
    ofstream file("userdata.csv", std::ios::app);// ���ļ�����׷��д��

    if (file.is_open() && file.good())
    { // ����ļ��Ƿ�ɹ���
        file << user->username << "," << user->password << endl;
        file.close(); // �ر��ļ�
    }
    else
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();
    }
}



bool Server::showLoadMenu(UserInfo* user)
{
    if (!clear(user))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. ������ĳɼ�"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. ȫ��top�ɼ�"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. ��ʼ��Ϸ"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. ���ط���"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���ѡ���ǣ�"))
        return false;
    return true;
}

bool Server::showRecentScores(UserInfo* user, int epollfd)
{
    int i = 0;

    if (!clear(user))
        return false;

    ifstream file("userdata.csv");

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
        logger->flush();
        return false;
    }

    string line;
    getline(file, line); // ������һ��

    while (getline(file, line))
    {
        istringstream ss(line);

        string cell;

        getline(ss, cell, ',');//�����û���

        if (cell == user->username)
        {
            getline(ss, cell, ',');//��������
            getline(ss, cell, ',');//������߷�
            getline(ss, cell, ',');//������߷ֶ�Ӧʱ��

            while (getline(ss, cell, ','))
            {
                if (cell != "")
                {
                    if (!outputText(user,WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, cell))
                        return false;
                    i++;
                }
            }
            file.close();
            break;
        }
        else
        {
            continue;
        }
    }

    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 20, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3���ز˵���"))
        return false;
    return true;
}

bool Server::showTopScores(UserInfo* user, int epollfd)
{
    if (!loadPlayerData())
        return false;

    if (!clear(user))
        return false;

    ifstream file("userdata.csv");

    vector<Player*> show;

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();
        return false;
    }

    string line;
    getline(file, line); // ������һ��

    while (getline(file, line))
    {
        istringstream ss(line);
        string cell;
        Player* gamer = new Player;
        getline(ss, gamer->playername, ',');//�����û���
        getline(ss, gamer->password, ',');//��������
        getline(ss, cell, ',');//������߷�

        if (cell != "")
        {
            gamer->Maximum_score = stoi(cell);
        }
        else
        {
            continue;
        }

        getline(ss, gamer->timestamp, ',');

        show.push_back(gamer);
    }

    file.close();

    sort(show.begin(), show.end(), cmp);

    int i = 0;

    if (show.size() <= 10)
    {
        for (auto& player : show)
        {
            string buffer = player->playername + " " + to_string(player->Maximum_score) + " " + player->timestamp;

            if (!outputText(user,WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, buffer))
                return false;

            i++;
        }
    }
    else
    {
        for (auto& player : show)
        {
            string buffer = player->playername + " " + to_string(player->Maximum_score) + " " + player->timestamp;

            if (!outputText(user,WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, buffer))
                return false;

            i++;

            if (i == 10)
            {
                break;
            }
        }
    }

    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 20, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3���ز˵���"))
        return false;
    return true;
}

bool Server::showGameDifficulty(UserInfo* user, int epollfd)
{
    if (!clear(user))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. ��"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. ��ͨ"))
        return false;
    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. ����"))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    return true;
}

int Server::returnToLoadMenu(UserInfo* user, int i, int epollfd)
{
    i++;

    int temp = this->ReceiveData(user,epollfd);

    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "3")
        {
            if (!this->showLoadMenu(user))
                return -1;
            return 1;
        }
        else
        {
            user->receivedata = "";
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + i, 1, COLOR_WHITE, emptyLine))
                return -1;
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������:"))
                return -1;
        }
    }

    return 2;
}

int Server::receive_username_register(UserInfo* user, int epollfd)
{
    string username = "";

    int temp = this->ReceiveData(user,epollfd);

    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata != "")
        {
            username = user->receivedata;
            user->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�û�������Ϊ�գ������������û�����"))
                return -1;
        }
    }

    if (username != "")
    {
        if (isUserExists(username))
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���û����ѱ�ע�ᣬ��ѡ�������û���:"))
                return -1;

            user->receivedata = "";
            return 0;
        }
        else
        {
            user->username = username;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���������룺"))
                return false;

            return 1;
        }
    }

    return 2;
}

int Server::receive_password_register(UserInfo* user, int epollfd)
{
    if (!loadPlayerData())
        return false;

    string password = "";

    int temp = this->ReceiveData(user,epollfd);

    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata != "")
        {
            password = user->receivedata;
            user->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���벻��Ϊ�գ��������������룺"))
                return -1;
            user->receivedata = "";
        }
    }

    if (password != "")
    {
        user->password = password;

        this->saveUserData(user);

        if (!outputText(user,WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ע��ɹ���"))
            return -1;

        if (!outputText(user,WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3�����ϼ��˵���"))
            return -1;

        return 1;
    }
    return 0;
}

int Server::receive_username_load(UserInfo* user, int epollfd)
{
    string username = "";

    int temp = this->ReceiveData(user,epollfd);

    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata != "")
        {
            username = user->receivedata;
            user->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�û�������Ϊ�գ������������û�����"))
                return -1;
        }
    }

    if (username != "")
    {
        user->username = username;

        if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���������룺"))
            return false;

        return 1;
    }

    return 2;
}

int Server::receive_password_load(UserInfo* user, int epollfd)
{
    if (!loadPlayerData())
        return false;

    string password = "";

    int temp = this->ReceiveData(user,epollfd);

    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata != "")
        {
            password = user->receivedata;
            user->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���벻��Ϊ�գ��������������룺"))
                return -1;
        }
    }

    if (password != "")
    {
        user->password = password;

        for (const auto& player : players)
        {
            if (player->playername == user->username && player->password == user->password)
            {
                user->Maximum_score = player->Maximum_score;

                for (const auto& score : player->scores)
                {
                    user->scores.push(score);
                }
                return 1;
            }
        }

        if (!outputText(user,WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��¼ʧ�ܣ��û������������"))
            return -1;

        if (!outputText(user,WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��3�����ϼ��˵�:"))
            return -1;
        return 0;
    }
    return 2;
}

int Server::loginUser(UserInfo* user, int epollfd)
{
    int temp = this->ReceiveData(user,epollfd);
    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "1")
        {
            if (!this->showRecentScores(user,epollfd))
            {
                return -1;
            }

            return 1;
        }
        else if (user->receivedata == "2")
        {
            if (!this->showTopScores(user,epollfd))
            {
                return -1;
            }

            return 2;
        }
        else if (user->receivedata == "3")
        {
            if (!this->showGameDifficulty(user,epollfd))
            {
                return -1;
            }
            return 3;
        }
        else if (user->receivedata == "4")
        {
            if (!this->showInitMenu(user))
            {
                return -1;
            }

            return 4;
        }
        else
        {
            if (!outputText(user,WINDOW_ROW_COUNT / 2 + 7, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������: "))
                return -1;
            user->receivedata = "";
        }
    }
    return 5;
}

bool Server::process_STATUS_NOTSTART(UserInfo* user, int epollfd)
{
    int temp = this->ReceiveData(user, epollfd);
    if (temp == -1)
    {
        delete user;
        return false;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "1")
        {
            if (!this->registerUser(user, epollfd))
                return false;
            user->status = STATUS_RECEIVE_USERNAME_REGISTER;
            user->receivedata = "";
        }
        else if (user->receivedata == "2")
        {
            if (!this->loadUser(user, epollfd))
                return false;
            user->status = STATUS_RECEIVE_USERNAME_LOAD;
            user->receivedata = "";
        }
        else
        {
            user->receivedata = "";
            std::string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 1, COLOR_WHITE, emptyLine))
                return false;
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������: "))
                return false;
        }
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_USERNAME_REGISTER(UserInfo* user, int epollfd)
{
    if (!loadPlayerData())
    {
        return false;
    }

    int key = this->receive_username_register(user, epollfd);

    if (key == -1) {
        return false;
    }
    else if (key == 1) {
        user->status = STATUS_RECEIVE_PASSWORD_REGISTER;
        user->receivedata = "";
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_PASSWORD_REGISTER(UserInfo* user, int epollfd)
{
    int key = this->receive_password_register(user, epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->status = STATUS_REGISTER_OR_LOAD_OVER;
        user->receivedata = "";
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_USERNAME_LOAD(UserInfo* user, int epollfd)
{
    int key = this->receive_username_load(user, epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->status = STATUS_RECEIVE_PASSWORD_LOAD;
        user->receivedata = "";
    }
    return true;
}

bool Server::process_STATUS_RECEIVE_PASSWORD_LOAD(UserInfo* user, int epollfd)
{
    int key = this->receive_password_load(user, epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        if (!this->showLoadMenu(user))
            return false;
        user->status = STATUS_LOGIN;
        user->receivedata = "";
    }
    else if (key == 0)
    {
        user->status = STATUS_REGISTER_OR_LOAD_OVER;
        user->receivedata = "";
    }
    return true;
}

bool Server::process_STATUS_LOGIN(UserInfo* user, int epollfd) 
{
    int key = this->loginUser(user, epollfd);

    if (key == -1)
    {
        return false; 
    }
    else if (key == 1)
    {
        user->receivedata = "";
        user->status = STATUS_LOGIN_OVER;
    }
    else if (key == 2)
    {
        user->receivedata = "";
        user->status = STATUS_LOGIN_OVER;
    }
    else if (key == 3)
    {
        user->receivedata = "";
        user->status = STATUS_SELECT_GAME_DIFFICULTY;
    }
    else if (key == 4)
    {
        user->receivedata = "";
        user->status = STATUS_NOTSTART;
    }
    return true;
}

bool Server::process_STATUS_LOGIN_OVER(UserInfo* user, int epollfd)
{
    int key = this->returnToLoadMenu(user, WINDOW_ROW_COUNT / 3 + 20, epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->status = STATUS_LOGIN;
        user->receivedata = "";
    }
    return true;
}

bool Server::process_STATUS_REGISTER_OR_LOAD_OVER(UserInfo* user, int epollfd)
{
    int key = this->returnToInitMenu(user, epollfd);
    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->status = STATUS_NOTSTART;
        user->receivedata = "";
    }
    return true;
}