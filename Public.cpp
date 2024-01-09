#include"Public.h"
#include"UserInfo.h"
#include"Player.h"
#include"Public_game.h"

extern vector<Player*> players;

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

extern map<int, UserInfo*> g_playing_gamer;

extern shared_ptr<spdlog::logger> logger;


bool IsSetSocketBlocking(int socket, bool blocking) {
    // 获取套接字标志
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        close(socket);
        //std::cerr << "Failed to get socket flags" << std::endl;
        logger->error("Failed to get socket flags");
        logger->flush();
        return false;
    }

    // 根据 blocking 参数切换回阻塞或非阻塞模式
    if (blocking) {
        flags &= ~O_NONBLOCK;  // 清除非阻塞标志
    }
    else {
        flags |= O_NONBLOCK;  // 设置非阻塞标志
    }

    // 设置套接字的新标志
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
        return COLOR_PURPLE;//“T”形方块设置为紫色
    case SHAPE_L:
    case SHAPE_J:
        return COLOR_RED;//“L”形和“J”形方块设置为红色
    case SHAPE_Z:
    case SHAPE_S:
        return COLOR_LOWBLUE;//“Z”形和“S”形方块设置为浅蓝色
    case SHAPE_O:
        return COLOR_YELLO;//“O”形方块设置为黄色
    case SHAPE_I:
        return COLOR_DEEPBLUE;//“I”形方块设置为深蓝色
    default:
        return COLOR_WHITE;
    }
}

void InitBlockInfo()
{
    int i;
    //“T”形
    auto& spaceT = blockDefines[SHAPE_T][0].space;
    for (i = 0; i <= 2; i++)
    {
        spaceT[1][i] = 1;
    }
    spaceT[2][1] = 1;

    //“L”形
    auto& spaceL = blockDefines[SHAPE_L][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceL[i][1] = 1;
    }
    spaceL[3][2] = 1;

    //“J”形
    auto& spaceJ = blockDefines[SHAPE_J][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceJ[i][2] = 1;
    }
    spaceJ[3][1] = 1;

    for (int i = 0; i <= 1; i++)
    {
        //“Z”形
        auto& spaceZ = blockDefines[SHAPE_Z][0].space;
        spaceZ[1][i] = 1;
        spaceZ[2][i + 1] = 1;
        //“S”形
        auto& spaceS = blockDefines[SHAPE_S][0].space;
        spaceS[1][i + 1] = 1;
        spaceS[2][i] = 1;
        //“O”形
        auto& spaceO = blockDefines[SHAPE_O][0].space;
        spaceO[1][i + 1] = 1;
        spaceO[2][i + 1] = 1;
    }

    //“I”形
    auto& spaceI = blockDefines[SHAPE_I][0].space;
    for (i = 0; i <= 3; i++)
    {
        spaceI[i][1] = 1;
    }

    for (int shape = 0; shape < 7; shape++)//7种形状
    {
        for (int form = 0; form < 3; form++)//4种形态（已经有了一种，这里每个还需要增加3种）
        {
            int temp[4][4] = { 0 };

            //获取第form种状态
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    temp[i][j] = blockDefines[shape][form].space[i][j];
                }
            }
            //将第form种形态顺时针旋转，得到第form+1种形态
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
        if (player->playername == playername)
        {
            return true;
        }
    }
    return false;
}

bool loadPlayerData()
{
    players.clear();

    ifstream file("userdata.csv");

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: %s\n", strerror(errno));
        logger->flush();
        return false;
    }

    string line;
    string column_name;
    getline(file, line); // 跳过第一行


    while (getline(file, line))
    {
        istringstream ss(line);

        string cell;

        Player* player = new Player;
        getline(ss, player->playername, ',');
        getline(ss, player->password, ',');

        if (player->playername == "" && player->password == "")
        {
            return true;
        }

        getline(ss, cell, ',');
        if (cell != "")
        {
            player->Maximum_score = stoi(cell);
        }
        else
        {
            player->Maximum_score = 0;
        }

        getline(ss, player->timestamp, ',');

        while (getline(ss, cell, ','))
        {
            if (cell != "")
            {
                player->scores.push_back(stoi(cell));
            }
            else
            {
                break;
            }
        }

        players.push_back(player);
    }

    file.close();
    return true;
}

string currenttime()//日志函数
{
    char timestamp[20];
    time_t now = time(nullptr);

    struct tm* timeinfo = localtime(&now);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    return timestamp;
}




