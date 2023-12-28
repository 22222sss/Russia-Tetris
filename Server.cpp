#include"Tetris.h"
#include"UserInfo.h"
#include"Server.h"
#include"Player.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, UserInfo*> g_playing_gamer;

Server::Server(int serverSocket,int timerfd) : serverSocket(serverSocket), timerfd(timerfd) {}

void Server::handleNewClientConnection(int epollfd)
{
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1)
    {
        printf("accept Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        return;
    }
    else
    {
        printf("Client[%d], welcome!\n", clientSocket);
        //Client.push_back(client);
    }

    // �����µ��û���Ϣ�ṹ��
    UserInfo* newUser = new UserInfo(clientSocket);

    // ���ÿͻ�������Ϊ������ģʽ
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    if (newUser == nullptr) 
    {
        close(clientSocket);
        printf("allocate memory for newUser Error In handleNewClientConnection");
        return;
    }


    // �������ӵ��¼���ӵ� epoll ʵ����

    struct epoll_event newEvent;
    newEvent.events = EPOLLIN | EPOLLET; // �������¼�����EPOLL��Ϊ��Ե����(Edge Triggered)ģʽ��
    newEvent.data.ptr = newUser; // ��ָ��ָ���û���Ϣ�ṹ��

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
    {
        printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        close(clientSocket);
        delete newUser;
        return;
    }

    if (!newUser->showInitMenu())
        return;
}

void Server::handleClientData(UserInfo* userInfo,int epollfd)
{
    // ���������ӿͻ��˵����ݽ����¼�
        if (userInfo->status == STATUS_PLAYING)
        {
            char buffer[1024];
            int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
            if (bytesRead == -1) {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                userInfo->Update_TopScore_RecentScore();
                printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
                return;
            }
            else if (bytesRead == 0) {
                // �ͻ��������ѹر�
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                userInfo->Update_TopScore_RecentScore();
                return;
            }


            // ������յ�������
            if (strcmp(buffer, KEY_DOWN) == 0)//��
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row + 1, userInfo->col) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
                {
                    //���������Ϸ��Ž������²���
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//�ÿո񸲸ǵ�ǰ��������λ��
                    {
                        return;
                    }

                    userInfo->row++; //��������������һ����ʾ����ʱ���൱��������һ���ˣ�

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (strcmp(buffer, KEY_LEFT) == 0)//��
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row, userInfo->col - 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
                {
                    //�������ƺ�Ϸ��Ž������²���
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//�ÿո񸲸ǵ�ǰ��������λ��
                    {
                        return;
                    }

                    userInfo->col--; //�������Լ�����һ����ʾ����ʱ���൱��������һ���ˣ�

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (strcmp(buffer, KEY_RIGHT) == 0)//��
            {
                if (userInfo->IsLegal(userInfo->shape, userInfo->form, userInfo->row, userInfo->col + 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
                {
                    //�������ƺ�Ϸ��Ž������²���
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//�ÿո񸲸ǵ�ǰ��������λ��
                    {
                        return;
                    }
                    userInfo->col++; //��������������һ����ʾ����ʱ���൱��������һ���ˣ�

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
            else if (*buffer == ' ')
            {
                if (userInfo->IsLegal(userInfo->shape, (userInfo->form + 1) % 4, userInfo->row + 1, userInfo->col) == 1) //�жϷ�����ת���Ƿ�Ϸ�
                {
                    //������ת��Ϸ��Ž������²���
                    if (!userInfo->DrawSpace(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//�ÿո񸲸ǵ�ǰ��������λ��
                    {
                        return;
                    }

                    userInfo->row++; //�������������ܲ���ԭ����ת�ɣ�
                    userInfo->form = (userInfo->form + 1) % 4; //�������̬��������һ����ʾ����ʱ���൱����ת�ˣ�

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
                    {
                        return;
                    }
                }
            }
        }
        else if (userInfo->status == STATUS_OVER_CONFIRMING)
        {
            char buffer[1024];
            int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
            if (bytesRead == -1) {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
                return;
            }
            else if (bytesRead == 0) {
                printf("Client[%d] disconnect!\n", userInfo->fd);
                // �ͻ��������ѹر�
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                return;
            }


            if (*buffer == 'Y' || *buffer == 'y')
            {

                if (!userInfo->showGameDifficulty(epollfd))
                {
                    return ;
                }

                userInfo->status = STATUS_SELECT_GAME_DIFFICULTY;

                userInfo->score = 0;
            }
            else if (*buffer == 'n' || *buffer == 'N')
            {

                // ������ɾ�� g_playing_gamer ��Ԫ��
                auto it = g_playing_gamer.find(userInfo->fd);
                if (it != g_playing_gamer.end()) {
                    g_playing_gamer.erase(it); // �� map ��ɾ��Ԫ��
                }

                userInfo->resetUserInfo();

                if(!userInfo->showInitMenu())
                    return;
            }
            else
            {
                if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ѡ��������ٴ�ѡ��"))
                    return;
            }
        }
        else if (userInfo->status == STATUS_NOTSTART)
        {
            int temp = userInfo->ReceiveData(epollfd);
            if (temp == -1)
            {
                delete userInfo;
                return;
            }
            else if (temp == 1)
            {
                if (userInfo->receivedata == "1")
                {   
                    if (!userInfo->registerUser(epollfd))
                        return;
                     
                    userInfo->status = STATUS_RECEIVE_USERNAME_REGISTER;
                    userInfo->receivedata = "";
                }
                else if (userInfo->receivedata == "2")
                {
                    if (!userInfo->loadUser(epollfd))
                        return;

                    userInfo->status = STATUS_RECEIVE_USERNAME_LOAD;
                    userInfo->receivedata = "";
                }
                else
                {
                    userInfo->receivedata = "";
                    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
                    if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 1, COLOR_WHITE, emptyLine))
                        return;
                    if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������: "))
                        return;
                }
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_USERNAME_REGISTER)
        {
            int key = userInfo->receive_username_register(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_RECEIVE_PASSWORD_REGISTER;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_PASSWORD_REGISTER)
        {
            int key = userInfo->receive_password_register(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_REGISTER_OR_LOAD_OVER;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_USERNAME_LOAD)
        {
            int key = userInfo->receive_username_load(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_RECEIVE_PASSWORD_LOAD;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_RECEIVE_PASSWORD_LOAD)
        {
            int key = userInfo->receive_password_load(epollfd);

            if (key == -1)
            {
                return; 
            }
            else if (key == 1)
            {
                if (!userInfo->showLoadMenu())
                    return;
                userInfo->status = STATUS_LOGIN;
                userInfo->receivedata = "";
            }
            else if (key==0)
            {
                userInfo->status = STATUS_REGISTER_OR_LOAD_OVER;
                userInfo->receivedata = "";
            }
            
        }
        else if (userInfo->status == STATUS_LOGIN)
        {
            int key = userInfo->loginUser(epollfd);

            if (key==-1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_LOGIN_OVER;
            }
            else if (key == 2)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_LOGIN_OVER;
            }
            else if (key == 3)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_SELECT_GAME_DIFFICULTY;
            }
            else if (key == 4)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_NOTSTART;
            }
        }
        else if (userInfo->status==STATUS_SELECT_GAME_DIFFICULTY)
        {
            int key = userInfo->select_game_difficulty(epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }
            else if (key == 2)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }
            else if (key == 3)
            {
                userInfo->receivedata = "";
                userInfo->status = STATUS_PLAYING;
            }

        }
        else if (userInfo->status == STATUS_LOGIN_OVER)
        {
            int key = userInfo->returnToLoadMenu(WINDOW_ROW_COUNT / 3 + 20, epollfd);

            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_LOGIN;
                userInfo->receivedata = "";
            }
        }
        else if (userInfo->status == STATUS_REGISTER_OR_LOAD_OVER)
        {
            int key = userInfo->returnToInitMenu(epollfd);
            if (key == -1)
            {
                return;
            }
            else if (key == 1)
            {
                userInfo->status = STATUS_NOTSTART;
                userInfo->receivedata = "";
            }
        }
}

void Server::processTimerEvent(int epollfd)
{
    for (auto i = g_playing_gamer.begin(); i != g_playing_gamer.end(); )
    {
        // ִ��һЩ�������
        if (i->second->status == STATUS_OVER_CONFIRMING)
        {
            auto eraseIter = i++;
            g_playing_gamer.erase(eraseIter); // ��ʹ�ú�׺�����������Ȼ����ɾ��Ԫ��
        }
        else if (i->second->status == STATUS_OVER_QUIT)
        {
            auto eraseIter = i++;
            printf("Client[%d] disconnected!\n", eraseIter->second->fd);
            close(eraseIter->second->fd);

            auto it = g_playing_gamer.find(eraseIter->second->fd);
            if (it != g_playing_gamer.end()) {
                g_playing_gamer.erase(eraseIter); // �� map ��ɾ��Ԫ��
            }
            delete eraseIter->second;

            if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr)) {
                perror("epoll_ctl EPOLL_CTL_DEL");
            }
        }
        else
        {
            i->second->handleTimedUserLogic();

            i++; // �ƶ�����һ��Ԫ��
        }
    }
}

void Server::processEvents(int readyCount, epoll_event* events, int epollfd)
{
    for (int i = 0; i < readyCount; ++i)
    {
        UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;

        if (currentFd == serverSocket)
        {
            handleNewClientConnection(epollfd);
        }
        else if (currentFd == timerfd)
        {
            if (!g_playing_gamer.empty())
            {
                processTimerEvent(epollfd);
            }
        }
        else
        {
            handleClientData(userInfo,epollfd);
        }
    }

}

void Server::Run(int epollfd)
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) {
            printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
            continue;
        }
        processEvents(readyCount, events ,epollfd);
    }
}