#include"Tetris.h"
#include"UserInfo.h"
#include"Server.h"



extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, UserInfo*> g_playing_gamer;

Server::Server(int serverSocket, int epollfd, int timerfd): serverSocket(serverSocket) , epollfd(epollfd), timerfd(timerfd){}

void Server::handleNewClientConnection()
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

    // ���ÿͻ�������Ϊ������ģʽ
    if (!IsSetSocketBlocking(clientSocket, false))
        return;

    // �����µ��û���Ϣ�ṹ��
    UserInfo* newUser = new UserInfo(clientSocket, STATUS_PLAYING, epollfd);

    if (newUser == nullptr) {
        close(clientSocket);
        printf("allocate memory for newUser Error In handleNewClientConnection");
        return;
    }

    g_playing_gamer.insert(make_pair(newUser->fd, newUser));

    // �������ӵ��¼���ӵ� epoll ʵ����

    struct epoll_event newEvent;
    newEvent.events = EPOLLIN | EPOLLET; // �������¼�����EPOLL��Ϊ��Ե����(Edge Triggered)ģʽ��
    newEvent.data.ptr = newUser; // ��ָ��ָ���û���Ϣ�ṹ��

    if (epoll_ctl(newUser->epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
    {
        printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
        close(clientSocket);
        g_playing_gamer.erase(newUser->fd);
        delete newUser;
        return;
    }

    if (!newUser->InitInterface())//��ʼ������
    {
        printf("Client[%d] InitInterface Error In handleNewClientConnection\n", newUser->fd);
        return;
    }
    InitBlockInfo(); //��ʼ��������Ϣ
    srand((unsigned int)time(NULL)); //������������ɵ����
    newUser->shape = rand() % 7;
    newUser->form = rand() % 4; //�����ȡ�������״����̬
    newUser->nextShape = rand() % 7;
    newUser->nextForm = rand() % 4;
    //�����ȡ��һ���������״����̬
    newUser->row = 1;
    newUser->col = WINDOW_COL_COUNT / 2 - 1; //�����ʼ����λ��



    if (!newUser->DrawBlock(newUser->nextShape, newUser->nextForm, 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
    {
        printf("Client[%d] Draw next Block Error In handleNewClientConnection\n", newUser->fd);
        return;
    }



    if (!newUser->DrawBlock(newUser->shape, newUser->form, newUser->row, newUser->col)) //���÷�����ʾ�ڳ�ʼ����λ��
    {
        printf("Client[%d] Draw Falling Block Error In handleNewClientConnection\n", newUser->fd);
        return;
    }
}

// ���崦���û��߼��ĺ���
void Server::processUserLogic(UserInfo* user)
{
    if (user->IsLegal(user->shape, user->form, user->row + 1, user->col) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (blockDefines[user->shape][user->form].space[i][j] == 1)
                {
                    user->data[user->row + i - 1][user->col + j - 1] = 1;
                    user->color[user->row + i - 1][user->col + j - 1] = user->shape;
                }
            }
        }

        user->line = 0;

        while (1)
        {
            if (user->Is_Increase_Score() == 1)
            {
                continue;
            }
            else if (user->Is_Increase_Score() == 0)
            {
                break;
            }
            else if (user->Is_Increase_Score() == -1)
            {
                return;
            }
        }

        if (!user->UpdateCurrentScore())
        {
            return;
        }

        if (!user->IsOver())//�ж��Ƿ����
        {
            user->shape = user->nextShape;
            user->form = user->nextForm;

            if (!user->DrawSpace(user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->nextShape = rand() % 7;
            user->nextForm = rand() % 4;

            user->row = 1;
            user->col = WINDOW_COL_COUNT / 2 - 1;

            if (!user->DrawBlock(user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
            {
                return;
            }

            if (!user->DrawBlock(user->shape, user->form, user->row, user->col))//���÷�����ʾ�ڳ�ʼ����λ��
            {
                return;
            }
        }

        else
        {
            if (!user->showover())
                return;
        }
    }
    else
    {

        if (!user->DrawSpace(user->shape, user->form, user->row, user->col))
        {
            return;
        }

        user->row++;

        if (!user->DrawBlock(user->shape, user->form, user->row, user->col))
        {
            return;
        }
    }
}

void Server::handleClientData(UserInfo* userInfo)
{
    // ���������ӿͻ��˵����ݽ����¼�
    char buffer[1024];
    int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
    if (bytesRead == -1) {
        userInfo->status = STATUS_OVER_QUIT;
        close(userInfo->fd);
        printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
    }
    else if (bytesRead == 0) {
        // �ͻ��������ѹر�
        userInfo->status = STATUS_OVER_QUIT;
        close(userInfo->fd);
    }
    else
    {
        if (userInfo->status == STATUS_PLAYING)
        {
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
            if (*buffer == 'Y' || *buffer == 'y')
            {
                auto it = find_if(g_playing_gamer.begin(), g_playing_gamer.end(), [userInfo](const pair<int, UserInfo*>& element) {
                    return element.second == userInfo;
                    });

                if (it == g_playing_gamer.end())
                {
                    userInfo->status = STATUS_PLAYING;

                    g_playing_gamer.insert(make_pair(userInfo->fd, userInfo));

                    userInfo->clear();

                    userInfo->score = 0;

                    if (!userInfo->InitInterface())//��ʼ������
                    {
                        return;
                    }
                    InitBlockInfo(); //��ʼ��������Ϣ
                    srand((unsigned int)time(NULL)); //������������ɵ����
                    userInfo->shape = rand() % 7;
                    userInfo->form = rand() % 4; //�����ȡ�������״����̬
                    userInfo->nextShape = rand() % 7;
                    userInfo->nextForm = rand() % 4;
                    //�����ȡ��һ���������״����̬
                    userInfo->row = 1;
                    userInfo->col = WINDOW_COL_COUNT / 2 - 1; //�����ʼ����λ��


                    if (!userInfo->DrawBlock(userInfo->nextShape, userInfo->nextForm, 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
                    {
                        return;
                    }

                    if (!userInfo->DrawBlock(userInfo->shape, userInfo->form, userInfo->row, userInfo->col)) //���÷�����ʾ�ڳ�ʼ����λ��
                    {
                        return;
                    }
                }
            }
            else if (*buffer == 'n' || *buffer == 'N')
            {
                userInfo->status = STATUS_OVER_QUIT;
                close(userInfo->fd);
                printf("Client[%d] disconnected!\n", userInfo->fd);
            }
            else
            {
                if (!userInfo->outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ѡ��������ٴ�ѡ��"))
                    return;
            }
        }
    }
}

void Server::processTimerEvent()
{
    if (!g_playing_gamer.empty())
    {
            // ��¼�ϴδ���ʱ��
            static std::chrono::steady_clock::time_point lastTriggerTime = std::chrono::steady_clock::now();

            // ��ȡ��ǰʱ��
            std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();

            // ��������ϴδ���������ʱ��
            std::chrono::duration<double> elapsed_time = currentTime - lastTriggerTime;

            // ���ʱ�����ڵ���1��
            if (elapsed_time >= std::chrono::seconds(1))
            {
                // ִ����Ӧ���߼�����

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
                        g_playing_gamer.erase(eraseIter);// ��ʹ�ú�׺�����������Ȼ����ɾ��Ԫ��
                        delete eraseIter->second;
                        epoll_ctl(eraseIter->second->epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr);
                    }
                    else
                    {
                        processUserLogic(i->second);
                        i++; // �ƶ�����һ��Ԫ��
                    }
                }
                // ���ϴδ���ʱ�����Ϊ��ǰʱ��
                lastTriggerTime = currentTime;
            }
    }
    
}

void Server::processEvents(int readyCount, epoll_event* events)
{
    for (int i = 0; i < readyCount; ++i)
    {
        UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
        int currentFd = events[i].data.fd;
        if (currentFd == serverSocket)
        {
            handleNewClientConnection();
        }
        else if (currentFd == timerfd)
        {
            processTimerEvent();
        }
        else
        {
            handleClientData(userInfo);
        }
    }
}

void Server::Run()
{
    while (1)
    {
        struct epoll_event events[MAXSIZE];
        int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
        if (readyCount == -1) {
            printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
            continue;
        }
        processEvents(readyCount, events);
    }
}