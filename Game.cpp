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

int Game::select_game_difficulty(Server* server,UserInfo* user, int epollfd)
{
    int temp = server->ReceiveData(user,epollfd);
    if (temp == -1)
    {
        delete user;
        return -1;
    }
    else if (temp == 1)
    {
        if (user->receivedata == "1")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 1;

            return 1;
        }
        else if (user->receivedata == "2")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 0.5;

            return 2;
        }
        else if (user->receivedata == "3")
        {
            if (!this->InitGameFace(user))
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(user->fd, user));

            user->speed = 0.2;

            return 3;
        }
        else
        {
            if (!outputText(user, WINDOW_ROW_COUNT / 2 + 7, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������: "))
                return -1;
            user->receivedata = "";
        }
    }
    return 4;
}

bool Game::InitInterface(UserInfo* user)
{
    for (int i = 0; i < WINDOW_ROW_COUNT; i++)
    {
        for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
        {
            if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
            {
                user->data[i][j] = 1; //��Ǹ�λ���з���

                if (!outputText(user,i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "��"))
                    return false;
            }
            else if (i == WINDOW_ROW_COUNT - 1)
            {
                user->data[i][j] = 1; //��Ǹ�λ���з���

                if (!outputText(user,i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "��"))
                    return false;
            }
            else
                user->data[i][j] = 0; //��Ǹ�λ���޷���
        }
    }

    for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
    {
        user->data[11][i] = 1; //��Ǹ�λ���з���
        if (!outputText(user,11 + 1, 2 * i + 1, COLOR_WHITE, "��"))
            return false;
    }

    if (!outputText(user,2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
        return false;


    if (!outputText(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
        return false;

    return true;
}

bool Game::InitGameFace(UserInfo* user)
{

    if (!clear(user))
        return false;

    if (!this->InitInterface(user))//��ʼ������
    {
        //printf("Client[%d] InitInterface Error In handleNewClientConnection\n", fd);
        //logger->error("Client[{}] InitInterface Error In handleNewClientConnection\n", user->fd);
        //logger->flush();
        return false;
    }

    InitBlockInfo(); //��ʼ��������Ϣ
    srand((unsigned int)time(NULL)); //������������ɵ����
    user->shape = rand() % 7;
    user->form = rand() % 4; //�����ȡ�������״����̬
    user->nextShape = rand() % 7;
    user->nextForm = rand() % 4;
    //�����ȡ��һ���������״����̬
    user->row = 1;
    user->col = WINDOW_COL_COUNT / 2 - 1; //�����ʼ����λ��

    if (!this->DrawBlock(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
    {
        return false;
    }

    if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) //���÷�����ʾ�ڳ�ʼ����λ��
    {
        return false;
    }
    return true;
}

bool Game::DrawBlock(UserInfo* user,int shape, int form, int row, int col)//row��col��ָ���Ƿ�����Ϣ���е�һ�е�һ�еķ���Ĵ�ӡλ��Ϊ��row�е�col��
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//�����λ���з���
            {
                if (!outputText(user,row + i, 2 * (col + j) - 1, Color(shape), "��"))
                    return false;
            }
        }
    }
    return true;
}

bool Game::DrawSpace(UserInfo* user,int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//�����λ���з���
            {
                if (!outputText(user,row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

bool Game::IsLegal(UserInfo* user,int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((blockDefines[shape][form].space[i][j] == 1) && (user->data[row + i - 1][col + j - 1] == 1))
                return false;
        }
    }
    return true;
}

int Game::Is_Increase_Score(UserInfo* user)
{
    int i = 0, j = 0;
    //�ж��Ƿ�÷�
    for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
    {
        int sum = 0;
        for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
        {
            sum += user->data[i][j];
        }
        if (sum == 0)
            break;
        if (sum == WINDOW_COL_COUNT - 2)//����ȫ�Ƿ��飬�ɵ÷�
        {
            user->line++;
            for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//����÷��еķ�����Ϣ
            {
                user->data[i][j] = 0;
                if (!outputText(user,i + 1, 2 * j + 1, COLOR_WHITE, "  "))
                    return -1;

            }
            //�ѱ���������������������Ųһ��
            for (int m = i; m > 1; m--)
            {
                sum = 0;//��¼��һ�еķ������
                for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
                {
                    sum += user->data[m - 1][n];//ͳ����һ�еķ������
                    user->data[m][n] = user->data[m - 1][n];//����һ�з���ı�ʶ�Ƶ���һ��
                    user->color[m][n] = user->color[m - 1][n];//����һ�з������ɫ����Ƶ���һ��
                    if (user->data[m][n] == 1)
                    {
                        if (!outputText(user,m + 1, 2 * n + 1, Color(user->color[m][n]), "��"))
                            return -1;
                    }
                    else
                    {
                        if (!outputText(user,m + 1, 2 * n + 1, COLOR_WHITE, "  "))
                            return -1;
                    }
                }
                if (sum == 0) //��һ����������ȫ�ǿո������ٽ��ϲ�ķ��������ƶ����ƶ�������
                    return 1; //����1����ʾ������øú��������жϣ��ƶ������Ŀ��ܻ������У�
            }
        }
    }
    return 0;
}

bool Game::UpdateCurrentScore(UserInfo* user)
{
    if (user->line >= 2)
    {
        user->score += (user->line + 1) * 10;

        if (!outputgrade(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->score))
        {
            return false;
        }
    }
    else
    {
        user->score += user->line * 10;

        if (!outputgrade(user,14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->score))
        {
            return false;
        }
    }
    return true;
}

bool Game::IsOver(UserInfo* user)
{
    //�ж���Ϸ�Ƿ����
    for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
    {
        if (user->data[1][j] == 1) //�����з�����ڣ��Ե�1��Ϊ���㣬���ǵ�0�У�
        {
            user->status = STATUS_OVER_CONFIRMING;
            return true;
        }
    }
    return false;
}

bool Game::showover(UserInfo* user)
{
    if (!outputText(user,WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(user,WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

void Game::resetUserInfo(UserInfo* user)
{
    user->line = 0;
    user->score = 0;
    user->username = "";
    user->status = STATUS_NOTSTART;
    user->password = "";
    user->Maximum_score = 0;
    user->scores = {};
    user->timestamp = "";
    memset(user->data, 0, sizeof(user->data));
    memset(user->color, -1, sizeof(user->color));
}





void Game::processBlockDown(UserInfo* user)
{
    if (this->IsLegal(user,user->shape, user->form, user->row + 1, user->col) == 0)
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
            if (this->Is_Increase_Score(user) == 1)
            {
                continue;
            }
            else if (this->Is_Increase_Score(user) == 0)
            {
                break;
            }
            else if (this->Is_Increase_Score(user) == -1)
            {
                return;
            }
        }

        if (!this->UpdateCurrentScore(user))
        {
            return;
        }

        if (!this->IsOver(user))//�ж��Ƿ����
        {
            user->shape = user->nextShape;
            user->form = user->nextForm;

            if (!this->DrawSpace(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->nextShape = rand() % 7;
            user->nextForm = rand() % 4;

            user->row = 1;
            user->col = WINDOW_COL_COUNT / 2 - 1;

            if (!this->DrawBlock(user,user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
            {
                return;
            }

            if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))//���÷�����ʾ�ڳ�ʼ����λ��
            {
                return;
            }
        }
        else
        {
            if (!Update_TopScore_RecentScore(user))
                return;

            if (!this->showover(user))
                return;
        }
    }
    else
    {

        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col))
        {
            return;
        }

        user->row++;

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))
        {
            return;
        }
    }
}

void Game::handleTimedUserLogic(UserInfo* user)
{
    // ��¼�ϴδ���ʱ��
    //this->lastTriggerTime = std::chrono::steady_clock::now();

    // ��ȡ��ǰʱ��
    user->currentTime = std::chrono::steady_clock::now();

    // ��������ϴδ���������ʱ��
    std::chrono::duration<double> elapsed_time = user->currentTime - user->lastTriggerTime;

    // ����ʱ���
    if (elapsed_time >= std::chrono::duration<double>(user->speed))
    {
        // ִ����Ӧ���߼�����
        this->processBlockDown(user);

        // ���ϴδ���ʱ�����Ϊ��ǰʱ��
        user->lastTriggerTime = user->currentTime;
    }
}

void Game::processTimerEvent(int epollfd)
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
            //printf("Client[%d] disconnected!\n", eraseIter->second->fd);
            logger->info("Client[{}] disconnected!\n", eraseIter->second->fd);
            logger->flush();
            close(eraseIter->second->fd);

            auto it = g_playing_gamer.find(eraseIter->second->fd);
            if (it != g_playing_gamer.end()) {
                g_playing_gamer.erase(eraseIter); // �� map ��ɾ��Ԫ��
            }
            delete eraseIter->second;

            if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr)) {
                perror("epoll_ctl EPOLL_CTL_DEL");
                logger->error("epoll_ctl EPOLL_CTL_DEL: {}", strerror(errno));
                logger->flush();
            }
        }
        else
        {
            this->handleTimedUserLogic(i->second);

            i++; // �ƶ�����һ��Ԫ��
        }
    }
}



//STATUS_PLAYING


bool Game::process_STATUS_PLAYING(UserInfo* user)
{
    char buffer[1024];
    int bytesRead = recv(user->fd, buffer, sizeof(buffer), 0);

    if (bytesRead == -1 || bytesRead == 0) {
        this->handleRecvError_STATUS_PLAYING(user);
        return false;
    }

    if (!this->handleReceivedData_STATUS_PLAYING(buffer, user))
        return false;
    return true;
}

void Game::handleRecvError_STATUS_PLAYING(UserInfo* user)
{
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
    Update_TopScore_RecentScore(user);
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
    logger->flush();
}

bool Game::handleMoveDown(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, user->form, user->row + 1, user->col) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //���������Ϸ��Ž������²���
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }

        user->row++; //��������������һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) {
            return false;
        }
    }

    return true;
}

bool Game::handleMoveLeft(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, user->form, user->row, user->col - 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //�������ƺ�Ϸ��Ž������²���
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }

        user->col--; //�������Լ�����һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) 
        {
            return false;
        }
    }

    return true;
}

bool Game::handleMoveRight(UserInfo* user)
{
    if (this->IsLegal(user,user->shape, user->form, user->row, user->col + 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //�������ƺ�Ϸ��Ž������²���
        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col))//�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }
        user->col++; //��������������һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col))
        {
            return false;
        }
    }

    return true;
}

bool Game::handleRotation(UserInfo* user) 
{
    if (this->IsLegal(user,user->shape, (user->form + 1) % 4, user->row + 1, user->col) == 1) //�жϷ�����ת���Ƿ�Ϸ� 
    {
        //������ת��Ϸ��Ž������²���

        if (!this->DrawSpace(user,user->shape, user->form, user->row, user->col)) //�ÿո񸲸ǵ�ǰ��������λ�� 
        {
            return false;
        }

        user->row++; //�������������ܲ���ԭ����ת�ɣ�
        user->form = (user->form + 1) % 4; //�������̬��������һ����ʾ����ʱ���൱����ת�ˣ�

        if (!this->DrawBlock(user,user->shape, user->form, user->row, user->col)) {
            return false;
        }
    }

    return true;
}

bool Game::handleReceivedData_STATUS_PLAYING(char* buffer, UserInfo* user)
{
    // ������յ�������
    if (strcmp(buffer, KEY_DOWN) == 0)//��
    {
        if (!this->handleMoveDown(user)) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_LEFT) == 0)//��
    {
        if (!this->handleMoveLeft(user))
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_RIGHT) == 0)//��
    {
        if (!this->handleMoveRight(user))
        {
            return false;
        }
    }
    else if (*buffer == ' ')
    {
        if (!this->handleRotation(user))
        {
            return false;
        }
    }

    return true;
}


//STATUS_OVER_CONFIRMING


void Game::handleRecvError_STATUS_OVER_CONFIRMING(UserInfo* user) {
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
    logger->flush();
}

void Game::handleDisconnect_STATUS_OVER_CONFIRMING(UserInfo* user) {
    logger->error("Client[{}] disconnect!\n", user->fd);
    logger->flush();
    user->status = STATUS_OVER_QUIT;
    close(user->fd);
}

bool Game::handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, Server* server, UserInfo* user, int epollfd)
{
    if (*buffer == 'Y' || *buffer == 'y')
    {
        if (!server->showGameDifficulty(user, epollfd))
        {
            return false;
        }
        user->status = STATUS_SELECT_GAME_DIFFICULTY;
        user->score = 0;
    }
    else if (*buffer == 'n' || *buffer == 'N')
    {
        auto it = g_playing_gamer.find(user->fd);
        if (it != g_playing_gamer.end())
        {
            g_playing_gamer.erase(it);
        }
        this->resetUserInfo(user);
        if (!server->showInitMenu(user))
            return false;
    }
    else
    {
        if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ѡ��������ٴ�ѡ��"))
            return false;
    }
    return true;
}

bool Game::process_STATUS_OVER_CONFIRMING(Server* server,UserInfo* user, int epollfd)
{
    char buffer[1024];
    int bytesRead = recv(user->fd, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        this->handleRecvError_STATUS_OVER_CONFIRMING(user);
        return false;
    }
    else if (bytesRead == 0) {
        this->handleDisconnect_STATUS_OVER_CONFIRMING(user);
        return false;
    }

    if (!this->handleBufferData_STATUS_OVER_CONFIRMING(buffer, server, user, epollfd))
        return false;

    return true;

}

//STATUS_SELECT_GAME_DIFFICULTY


bool Game::process_STATUS_SELECT_GAME_DIFFICULTY(Server *server, UserInfo* user, int epollfd)
{
    int key = this->select_game_difficulty(server,user,epollfd);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    else if (key == 2)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    else if (key == 3)
    {
        user->receivedata = "";
        user->status = STATUS_PLAYING;
    }
    return true;
}

