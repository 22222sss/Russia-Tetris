#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern shared_ptr<spdlog::logger> logger;


int TetrisGame::select_game_difficulty(User* user)
{
    
    if (!Filedata::loadPlayerData())
        return false;


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
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("EASY");
            
            user->setSpeed(1);


            user->initUserInfo();

            return 1;
        }
        else if (user->getReceivedata() == "2")
        {
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("NORMAL");

            user->setSpeed(0.5);

            user->initUserInfo();
            
            return 2;
        }
        else if (user->getReceivedata() == "3")
        {
            if (!UImanage::InitGameFace(user))
            {
                return -1;
            }

            user->setGameDiffculty("DIFFICULT");

            user->setSpeed(0.2);

            user->initUserInfo();

            return 3;
        }
        else if (user->getReceivedata() == "4")
        {
            if (!UImanage::showLoadMenu(user))
                return false;

            user->setStatus(STATUS_LOGIN);
            user->setReceivedata("");
            
            return 4;
        }
        else
        {
            if (!UImanage::show_Error_Message(WINDOW_ROW_COUNT / 2 + 10, user))
                return false;

            user->setReceivedata("");
        }
    }
    return 5;
}

bool TetrisGame::IsLegal(User* user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((blockDefines[shape][form].space[i][j] == 1) && (user->getData(row + i - 1, col + j - 1) == 1))
                return false;
        }
    }
    return true;
}

int TetrisGame::Is_Increase_Score(User* user)
{
    int i = 0, j = 0;
    //�ж��Ƿ�÷�
    for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
    {
        int sum = 0;
        for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
        {
            sum += user->getData(i, j);
        }
        if (sum == 0)
            break;
        if (sum == WINDOW_COL_COUNT - 2)//����ȫ�Ƿ��飬�ɵ÷�
        {
            user->setLine(user->getLine() + 1);

            for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//����÷��еķ�����Ϣ
            {
                user->setData(i, j, 0);
                if (!outputText(user, i + 1, 2 * j + 1, COLOR_WHITE, "  "))
                    return -1;

            }
            //�ѱ���������������������Ųһ��
            for (int m = i; m > 1; m--)
            {
                sum = 0;//��¼��һ�еķ������
                for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
                {
                    sum += user->getData(m - 1, n);//ͳ����һ�еķ������
                    user->setData(m, n, user->getData(m - 1, n));//����һ�з���ı�ʶ�Ƶ���һ��
                    user->setColor(m, n, user->getColor(m - 1, n));//����һ�з������ɫ����Ƶ���һ��
                    if (user->getData(m, n) == 1)
                    {
                        if (!outputText(user, m + 1, 2 * n + 1, Color(user->getColor(m, n)), "��"))
                            return -1;
                    }
                    else
                    {
                        if (!outputText(user, m + 1, 2 * n + 1, COLOR_WHITE, "  "))
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

bool TetrisGame::UpdateCurrentScore(User* user)
{
    if (user->getLine() >= 2)
    {
        user->setScore(user->getScore()+(user->getLine() + 1) * 10);

        if (!outputgrade(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    else
    {
        user->setScore(user->getScore()+user->getLine() * 10);

        if (!outputgrade(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", user->getScore()))
        {
            return false;
        }
    }
    return true;
}

bool TetrisGame::IsOver(User* user)
{
    //�ж���Ϸ�Ƿ����
    for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
    {
        if (user->getData(1, j) == 1) //�����з�����ڣ��Ե�1��Ϊ���㣬���ǵ�0�У�
        {
            user->setStatus(STATUS_OVER_CONFIRMING);
            return true;
        }
    }
    return false;
}



//STATUS_PLAYING


bool TetrisGame::process_STATUS_PLAYING(User* user)
{
    char buffer[1024] = { 0 };
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1 || bytesRead == 0) {
        TetrisGame::handleRecvError_STATUS_PLAYING(user);
        return false;
    }

    buffer[bytesRead] = '\0';

    if (!TetrisGame::handleReceivedData_STATUS_PLAYING(buffer, user))
        return false;
    return true;
}

void TetrisGame::handleRecvError_STATUS_PLAYING(User* user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

bool TetrisGame::handleMoveDown(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //���������Ϸ��Ž������²���
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//��������������һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleMoveLeft(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() - 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //�������ƺ�Ϸ��Ž������²���
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }

        user->setCol(user->getCol() - 1); //�������Լ�����һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleMoveRight(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow(), user->getCol() + 1) == 1) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
    {
        //�������ƺ�Ϸ��Ž������²���
        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//�ÿո񸲸ǵ�ǰ��������λ��
        {
            return false;
        }
        user->setCol(user->getCol() + 1); //��������������һ����ʾ����ʱ���൱��������һ���ˣ�

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleRotation(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), (user->getForm() + 1) % 4, user->getRow() + 1, user->getCol()) == 1) //�жϷ�����ת���Ƿ�Ϸ� 
    {
        //������ת��Ϸ��Ž������²���

        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //�ÿո񸲸ǵ�ǰ��������λ�� 
        {
            return false;
        }

        user->setRow(user->getRow() + 1);//�������������ܲ���ԭ����ת�ɣ�
        user->setForm((user->getForm() + 1) % 4); //�������̬��������һ����ʾ����ʱ���൱����ת�ˣ�

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) {
            return false;
        }
    }

    return true;
}

bool TetrisGame::handleReceivedData_STATUS_PLAYING(char* buffer, User* user)
{
    // ������յ�������
    if (strcmp(buffer, KEY_DOWN) == 0)//��
    {
        if (!TetrisGame::handleMoveDown(user)) //�жϷ��������ƶ�һλ���Ƿ�Ϸ�
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_LEFT) == 0)//��
    {
        if (!TetrisGame::handleMoveLeft(user))
        {
            return false;
        }
    }
    else if (strcmp(buffer, KEY_RIGHT) == 0)//��
    {
        if (!TetrisGame::handleMoveRight(user))
        {
            return false;
        }
    }
    else if (*buffer == ' ')
    {
        if (!TetrisGame::handleRotation(user))
        {
            return false;
        }
    }

    return true;
}


//STATUS_OVER_CONFIRMING


void TetrisGame::handleRecvError_STATUS_OVER_CONFIRMING(User* user)
{
    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] recv Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
    logger->flush();
}

void TetrisGame::handleDisconnect_STATUS_OVER_CONFIRMING(User* user) 
{

    user->setStatus(STATUS_OVER_QUIT);
    close(user->getFd());
    logger->error("Client[{}] disconnect!\n", user->getFd());
    logger->flush();
}

bool TetrisGame::handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, User* user)
{

    if (*buffer == 'Y' || *buffer == 'y')
    {
        if (!UImanage::showGameDifficulty(user))
        {
            return false;
        }

        user->setStatus(STATUS_SELECT_GAME_DIFFICULTY);
        user->setScore(0);
        user->setScores({});
        user->setGameDiffclutys({});
    }
    else if (*buffer == 'n' || *buffer == 'N')
    {
        user->resetUserInfo();

        user->setStatus(STATUS_NOTSTART);
        if (!UImanage::showInitMenu(user))
            return false;

    }
    else
    {
        if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ѡ��������ٴ�ѡ��"))
            return false;
    }
    return true;
}

bool TetrisGame::process_STATUS_OVER_CONFIRMING(User* user)
{
    char buffer[1024];
    int bytesRead = recv(user->getFd(), buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        TetrisGame::handleRecvError_STATUS_OVER_CONFIRMING(user);
        return false;
    }
    else if (bytesRead == 0) {
        TetrisGame::handleDisconnect_STATUS_OVER_CONFIRMING(user);
        return false;
    }

    if (!TetrisGame::handleBufferData_STATUS_OVER_CONFIRMING(buffer, user))
        return false;

    return true;

}

//STATUS_SELECT_GAME_DIFFICULTY


bool TetrisGame::process_STATUS_SELECT_GAME_DIFFICULTY(User* user)
{
    int key = TetrisGame::select_game_difficulty(user);

    if (key == -1)
    {
        return false;
    }
    else if (key == 1)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 2)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 3)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_PLAYING);
    }
    else if (key == 4)
    {
        user->setReceivedata("");
        user->setStatus(STATUS_LOGIN);
    }
    return true;
}

//����ʱ���¼�����--���̲�����ʱ�����Զ��½�
void TetrisGame::processBlockDown(User* user)
{

    if (TetrisGame::IsLegal(user, user->getShape(), user->getForm(), user->getRow() + 1, user->getCol()) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (blockDefines[user->getShape()][user->getForm()].space[i][j] == 1)
                {
                    user->setData(user->getRow() + i - 1, user->getCol() + j - 1, 1);

                    user->setColor(user->getRow() + i - 1, user->getCol() + j - 1, user->getShape());
                }
            }
        }



        user->setLine(0);

        while (1)
        {
            if (TetrisGame::Is_Increase_Score(user) == 1)
            {
                continue;
            }
            else if (TetrisGame::Is_Increase_Score(user) == 0)
            {
                break;
            }
            else if (TetrisGame::Is_Increase_Score(user) == -1)
            {
                return;
            }
        }

        if (!TetrisGame::UpdateCurrentScore(user))
        {
            return;
        }

        if (!TetrisGame::IsOver(user))//�ж��Ƿ����
        {
            user->setShape(user->getNextShape());
            user->setForm(user->getNextForm());

            if (!UImanage::DrawSpace(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            user->setNextShape(rand() % 7);
            user->setNextForm(rand() % 4);

            user->setRow(1);
            user->setCol(WINDOW_COL_COUNT / 2 - 1);

            if (!UImanage::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
            {
                return;
            }

            if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))//���÷�����ʾ�ڳ�ʼ����λ��
            {
                return;
            }
        }
        else
        {
            if (!Filedata::Update_TopScore_RecentScore(user))
                return;

            if (!UImanage::showover(user))
                return;
        }
    }
    else
    {

        if (!UImanage::DrawSpace(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }

        user->setRow(user->getRow() + 1);

        if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol()))
        {
            return;
        }
    }
}

void TetrisGame::handleTimedUserLogic(User* user)
{
    // ��ȡ��ǰʱ��
    user->setCurrentTime(std::chrono::steady_clock::now());

    // ��������ϴδ���������ʱ��
    std::chrono::duration<double> elapsed_time = user->getCurrentTime() - user->getLastTriggerTime();

    // ����ʱ���
    if (elapsed_time >= std::chrono::duration<double>(user->getSpeed()))
    {
        // ִ����Ӧ���߼�����
        processBlockDown(user);

        // ���ϴδ���ʱ�����Ϊ��ǰʱ��
        user->setLastTriggerTime(user->getCurrentTime());
    }
}

void TetrisGame::processTimerEvent(int timerfd, short events, void* arg)
{
    if (!User::getUsers().empty())
    {
        for (auto i = User::getUsers().begin(); i != User::getUsers().end(); i++)
        {
            if (i->second->getStatus() == STATUS_PLAYING)
            {
                handleTimedUserLogic(i->second);
            }
        }
    }
}
