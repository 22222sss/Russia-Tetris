#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

UImanage::UImanage(){}

bool UImanage::showInitMenu(User* user)
{
    if (!UImanage::clear(user))
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

bool UImanage::show_Receive_Username(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�������û�����"))
        return false;
    return true;
}

bool UImanage::show_Error_Message(int i,User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;
    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�����������������: "))
        return false;
    return true;
}

bool UImanage::show_Username_Empty_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�û�������Ϊ�գ������������û�����"))
        return false;
    return true;
}

bool UImanage::show_Username_Taken_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���û����ѱ�ע�ᣬ��ѡ�������û���:"))
        return false;
    return true;
}

bool UImanage::show_Receive_Password(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���������룺"))
        return false;
    return true;
}

bool UImanage::show_Password_Empty_Error(int i, User* user)
{
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
        return false;

    if (!outputText(user, i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���벻��Ϊ�գ��������������룺"))
        return false;
    return true;
}

bool UImanage::show_Register_Success(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "ע��ɹ���"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3�����ϼ��˵���"))
        return false;
    return true;
}

bool UImanage::show_Login_Failure(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��¼ʧ�ܣ��û������������"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��3�����ϼ��˵�:"))
        return false;
    return true;
}

bool UImanage::showLoadMenu(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. ������ĳɼ�"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. ȫ��top�ɼ�"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. ��ʼ��Ϸ"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 8, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. ���ط���"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 12, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "���ѡ���ǣ�"))
        return false;
    return true;
}

bool UImanage::showRecentScores(User* user)
{

    int i = 0;

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


    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 21, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3���ز˵���"))
        return false;
    return true;
}

bool UImanage::showTopScores(User* user)
{
    
    if (!Filedata::loadPlayerData())
        return false;

    ifstream file("userdata.csv");//���·�����������Ŀ�ִ���ļ����ڵ�Ŀ¼��

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();

        return false;
    }

    vector<PlayerInfo*> show = Filedata::Read_AllpalyerInfo(file);

    if (!UImanage::clear(user))
        return false;

    sort(show.begin(), show.end(), cmp_easy);

    UImanage::showTopScores_Easy(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT / 3, show, user);

    sort(show.begin(), show.end(), cmp_normal);

    UImanage::showTopScores_Normal(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 20, show, user);

    sort(show.begin(), show.end(), cmp_diffcult);

    UImanage::showTopScores_Diffcult(WINDOW_ROW_COUNT / 3, WINDOW_COL_COUNT + 54, show, user);

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 40, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "�밴3���ز˵���"))
        return false;
    return true;
}

bool UImanage::showTopScores_Easy(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "��ģʽ��"))
        return false;

    i += 2;

    if (!outputText(user, row + i, col, COLOR_WHITE, "�û��� ���� �û���ȡ��߷ֵ�ʱ��"))
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

bool UImanage::showTopScores_Normal(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "��ͨģʽ��"))
        return false;

    i += 2; //i=16

    if (!outputText(user, row + i, col, COLOR_WHITE, "�û��� ���� �û���ȡ��߷ֵ�ʱ��"))
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

bool UImanage::showTopScores_Diffcult(int row, int col, vector<PlayerInfo*> show, User* user)
{
    int i = 0, j = 0;

    if (!outputText(user, row + i, col, COLOR_WHITE, "����ģʽ��"))
        return false;

    i += 2; //i=28

    if (!outputText(user, row + i, col, COLOR_WHITE, "�û��� ���� �û���ȡ��߷ֵ�ʱ��"))
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

bool UImanage::showGameDifficulty(User* user)
{
    if (!UImanage::clear(user))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. ��ģʽ"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. ��ͨģʽ"))
        return false;
    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. ����ģʽ"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. �����ϼ��˵�"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 8, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "��ѡ�������"))
        return false;
    return true;
}

bool UImanage::InitInterface(User* user)
{
    for (int i = 0; i < WINDOW_ROW_COUNT; i++)
    {
        for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
        {
            if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
            {
                user->setData(i, j, 1); //��Ǹ�λ���з���

                if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "��"))
                    return false;
            }
            else if (i == WINDOW_ROW_COUNT - 1)
            {
                user->setData(i, j, 1); //��Ǹ�λ���з���

                if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "��"))
                    return false;
            }
            else
                user->setData(i, j, 0); //��Ǹ�λ���޷���
        }
    }

    for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
    {
        user->setData(11, i, 1);//��Ǹ�λ���з���
        if (!outputText(user, 11 + 1, 2 * i + 1, COLOR_WHITE, "��"))
            return false;
    }

    if (!outputText(user, 2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
        return false;


    if (!outputText(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
        return false;

    return true;
}

bool UImanage::InitGameFace(User* user)
{

    if (!UImanage::clear(user))
        return false;

    if (!UImanage::InitInterface(user))//��ʼ������
    {
        return false;
    }

    InitBlockInfo(); //��ʼ��������Ϣ
    srand((unsigned int)time(NULL)); //������������ɵ����

    user->setShape(rand() % 7);
    user->setForm(rand() % 4); //�����ȡ�������״����̬
    user->setNextShape(rand() % 7);

    user->setNextForm(rand() % 4);
    //�����ȡ��һ���������״����̬
    user->setRow(1);
    user->setCol(WINDOW_COL_COUNT / 2 - 1); //�����ʼ����λ��

    if (!UImanage::DrawBlock(user, user->getNextShape(), user->getNextForm(), 3, WINDOW_COL_COUNT + 3))//����һ��������ʾ�����Ͻ�
    {
        return false;
    }

    if (!UImanage::DrawBlock(user, user->getShape(), user->getForm(), user->getRow(), user->getCol())) //���÷�����ʾ�ڳ�ʼ����λ��
    {
        return false;
    }
    return true;
}

bool UImanage::DrawBlock(User* user, int shape, int form, int row, int col)//row��col��ָ���Ƿ�����Ϣ���е�һ�е�һ�еķ���Ĵ�ӡλ��Ϊ��row�е�col��
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//�����λ���з���
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, Color(shape), "��"))
                    return false;
            }
        }
    }
    return true;
}

bool UImanage::DrawSpace(User* user, int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//�����λ���з���
            {
                if (!outputText(user, row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

bool UImanage::showover(User* user)
{
    if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

bool UImanage::clear(User* user)
{
    int i;
    string emptyLine(6 * WINDOW_COL_COUNT, ' ');
    for (i = 1; i <= WINDOW_ROW_COUNT * 10; i++)
    {
        if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
            return false;
    }
    return true;
}