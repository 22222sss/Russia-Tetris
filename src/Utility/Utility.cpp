#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

extern vector<PlayerInfo*> players;

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, User*> users;

extern shared_ptr<spdlog::logger> logger;

bool output(User* user, string s)
{
    int bytesSent = send(user->getFd(), s.c_str(), s.length(), 0);
    if (bytesSent == -1)
    {
        /*
        if (user->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore(user);
        }
        */
        user->setStatus(STATUS_OVER_QUIT);

        close(user->getFd());
        //printf("Client[%d] send Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        logger->error("Client[{}] send Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
        logger->flush();
        return false;
    }
    else if (bytesSent == 0)
    {
        /*
        if (user->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore(user);
        }
        */
        // �ͻ��������ѹر�
        user->setStatus(STATUS_OVER_QUIT);

        close(user->getFd());
        return false;
    }
    return true;
}

bool moveTo(User* user, int row, int col) {
    // ��������Ƿ�Ϸ�
    if (row < 0 || col < 0) {
        //cerr << "Invalid row or col number" << endl;

        // ���Ը��ݾ������������Ӧ�Ĵ��������
        logger->error("Invalid row or col number\n");
        logger->flush();
        return false;

    }

    string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
    if (!output(user, command))
        return false;
    return true;
}

bool ChangeCurrentColor(User* user, int n)
{
    // ��������Ƿ�Ϸ�
    if (n < 0 || n > 255) {
        //cerr << "Invalid color number" << endl;
        // ���Ը��ݾ������������Ӧ�Ĵ��������
        logger->error("Invalid row or col number\n");
        logger->flush();
        return false;
    }

    string command = "\33[" + to_string(n) + "m";
    if (!output(user, command))
        return false;
    return true;
}

bool outputText(User* user, int row, int col, int n, string s)
{
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!moveTo(user, row, 0))
        {
            return false;
        }

        if (!output(user, string("  ")))
        {
            return false;
        }

        if (!moveTo(user, row, 0))
        {
            return false;
        }

        if (!ChangeCurrentColor(user, COLOR_WHITE))
        {
            return false;
        }

        if (!output(user, string("��")))
        {
            return false;
        }
    }


    if (!moveTo(user, row, col))
    {
        return false;
    }
    if (!ChangeCurrentColor(user, n))
    {
        return false;
    }
    if (!output(user, s))
    {
        return false;
    }


    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!moveTo(user, row, 500))
        {
            return false;
        }

        if (!output(user, string(" ")))
        {
            return false;
        }
    }

    return true;
}

bool outputgrade(User* user, int row, int col, int n, string s, int grade)
{
    // ��������Ƿ�Ϸ�
    if (grade < 0) {
        //cerr << "Invalid grades: " << grade << endl;
        // ���Ը��ݾ������������Ӧ�Ĵ��������
        logger->error("Invalid grades: {}", grade);
        logger->flush();
        return false;
    }

    if (!moveTo(user, row, col))
    {
        return false;
    }
    if (!ChangeCurrentColor(user, n))
    {
        return false;
    }

    string command = s + to_string(grade);

    if (!output(user, command))
    {
        return false;
    }
    return true;
}

bool IsSetSocketBlocking(int socket, bool blocking) {
    // ��ȡ�׽��ֱ�־
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        close(socket);
        //std::cerr << "Failed to get socket flags" << std::endl;
        logger->error("Failed to get socket flags");
        logger->flush();
        return false;
    }

    // ���� blocking �����л��������������ģʽ
    if (blocking) {
        flags &= ~O_NONBLOCK;  // �����������־
    }
    else {
        flags |= O_NONBLOCK;  // ���÷�������־
    }

    // �����׽��ֵ��±�־
    if (fcntl(socket, F_SETFL, flags) < 0) {
        close(socket);
        //std::cerr << "Failed to set socket mode" << std::endl;
        logger->error("Failed to get socket flags");
        logger->flush();
        return false;
    }
    return true;
}

int Color(int c)
{
    switch (c)
    {
    case SHAPE_T:
        return COLOR_PURPLE;//��T���η�������Ϊ��ɫ
    case SHAPE_L:
    case SHAPE_J:
        return COLOR_RED;//��L���κ͡�J���η�������Ϊ��ɫ
    case SHAPE_Z:
    case SHAPE_S:
        return COLOR_LOWBLUE;//��Z���κ͡�S���η�������Ϊǳ��ɫ
    case SHAPE_O:
        return COLOR_YELLO;//��O���η�������Ϊ��ɫ
    case SHAPE_I:
        return COLOR_DEEPBLUE;//��I���η�������Ϊ����ɫ
    default:
        return COLOR_WHITE;
    }
}

void InitBlockInfo()
{
    int i;
    //��T����
    auto& spaceT = blockDefines[SHAPE_T][0].space;
    for (i = 0; i <= 2; i++)
    {
        spaceT[1][i] = 1;
    }
    spaceT[2][1] = 1;

    //��L����
    auto& spaceL = blockDefines[SHAPE_L][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceL[i][1] = 1;
    }
    spaceL[3][2] = 1;

    //��J����
    auto& spaceJ = blockDefines[SHAPE_J][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceJ[i][2] = 1;
    }
    spaceJ[3][1] = 1;

    for (int i = 0; i <= 1; i++)
    {
        //��Z����
        auto& spaceZ = blockDefines[SHAPE_Z][0].space;
        spaceZ[1][i] = 1;
        spaceZ[2][i + 1] = 1;
        //��S����
        auto& spaceS = blockDefines[SHAPE_S][0].space;
        spaceS[1][i + 1] = 1;
        spaceS[2][i] = 1;
        //��O����
        auto& spaceO = blockDefines[SHAPE_O][0].space;
        spaceO[1][i + 1] = 1;
        spaceO[2][i + 1] = 1;
    }

    //��I����
    auto& spaceI = blockDefines[SHAPE_I][0].space;
    for (i = 0; i <= 3; i++)
    {
        spaceI[i][1] = 1;
    }

    for (int shape = 0; shape < 7; shape++)//7����״
    {
        for (int form = 0; form < 3; form++)//4����̬���Ѿ�����һ�֣�����ÿ������Ҫ����3�֣�
        {
            int temp[4][4] = { 0 };

            //��ȡ��form��״̬
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    temp[i][j] = blockDefines[shape][form].space[i][j];
                }
            }
            //����form����̬˳ʱ����ת���õ���form+1����̬
            for (i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    blockDefines[shape][form + 1].space[i][j] = temp[3 - j][i];
                }
            }
        }
    }
}

bool isUserExists(const string& playername)
{

    for (auto& player : players)
    {
        if (player->getPlayerName() == playername)
        {
            return true;
        }
    }
    return false;
}

string currenttime()//��־����
{
    char timestamp[20];
    time_t now = time(nullptr);

    struct tm* timeinfo = localtime(&now);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    return timestamp;
}

// ����ַ����Ƿ�Ϊ����
bool isNumber(const std::string& s) 
{
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}