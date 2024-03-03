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

Filedata::Filedata(){}

vector<string> Filedata::Read_recent_grades(User* user)
{
    vector<string> temp = {};
    ifstream file("userdata.csv");//相对路径是相对于你的可执行文件所在的目录的

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
        logger->flush();

        temp.push_back("-1");
        return temp;
    }

    string line;
    getline(file, line); // 跳过第一行

    while (getline(file, line))
    {
        istringstream ss(line);

        string cell;

        getline(ss, cell, ',');//跳过用户名

        if (cell == user->getUsername())
        {
            getline(ss, cell, ',');//跳过密码

            getline(ss, cell, ',');//跳过EASY模式最高分
            getline(ss, cell, ',');//跳过难度
            getline(ss, cell, ',');//跳过最高分对应时间

            getline(ss, cell, ',');//跳过NORMAL模式最高分
            getline(ss, cell, ',');//跳过难度
            getline(ss, cell, ',');//跳过最高分对应时间

            getline(ss, cell, ',');//跳过DIFFCULT模式最高分
            getline(ss, cell, ',');//跳过难度
            getline(ss, cell, ',');//跳过最高分对应时间

            while (getline(ss, cell, ','))
            {
                string outcome = cell;

                getline(ss, cell, ',');

                outcome += "  " + cell;

                if (cell != "")
                {
                    temp.push_back(outcome);
                }
            }
            file.close();
            return temp;
            break;
        }
        else
        {
            continue;
        }
    }

    temp.push_back("1");

    return temp;
}

vector<PlayerInfo*> Filedata::Read_AllpalyerInfo(ifstream &file)
{
    vector<PlayerInfo*> show = {};

    string line;
    getline(file, line); // 跳过第一行

    while (getline(file, line))
    {
        istringstream ss(line);
        string cell;
        PlayerInfo* gamer = new PlayerInfo;

        string playerName, Password;

        getline(ss, playerName, ',');//跳过用户名
        gamer->setPlayerName(playerName);

        getline(ss, Password, ',');//跳过密码
        gamer->setPassword(Password);

        getline(ss, cell, ',');//跳过EASY模式最高分

        if (cell != "")
        {
            gamer->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            //continue;
            gamer->setMaximum_Score_Easy(0);
        }

        getline(ss, cell, ',');//跳过EASY模式

        string TimestampEasy;
        getline(ss, TimestampEasy, ',');

        gamer->setTimestampEasy(TimestampEasy);


        getline(ss, cell, ',');//跳过NORMAL模式最高分

        if (cell != "")
        {
            gamer->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            //continue;
            gamer->setMaximum_Score_Normal(0);
        }

        getline(ss, cell, ',');//跳过NORMAL模式

        string TimestampNormal;
        getline(ss, TimestampNormal, ',');

        gamer->setTimestampNormal(TimestampNormal);
        getline(ss, cell, ',');//跳过DIFFCULT模式最高分

        if (cell != "")
        {
            gamer->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            //continue;
            gamer->setMaximum_Score_Difficult(0);
        }

        getline(ss, cell, ',');//跳过DIFFCULT模式

        string TimestampDifficult;
        getline(ss, TimestampDifficult, ',');

        gamer->setTimestampDifficult(TimestampDifficult);

        show.push_back(gamer);
    }

    file.close();

    return show;
}

bool Filedata::saveNewUserData(User* user)
{
    //相对路径是相对于你的可执行文件所在的目录的
    ofstream file("userdata.csv", std::ios::app);// 打开文件进行追加写入

    if (file.is_open() && file.good())
    { // 检查文件是否成功打开
        file << user->getUsername() << "," << user->getPassword() << ",";

        file << "" << "," << "EASY" << "," << "" << ",";

        file << "" << "," << "NORMAL" << "," << "" << ",";

        file << "" << "," << "DIFFCULT" << "," << "" << ",";

        file << endl;

        file.close(); // 关闭文件
    }
    else
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();
        return false;
    }
    return true;
}

bool Filedata::loadPlayerData()
{
    players.clear();

    ifstream file("userdata.csv"); //相对路径是相对于你的可执行文件所在的目录的

    if (!file.is_open())
    {
        //std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
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

        PlayerInfo* player = new PlayerInfo;

        string playerName, password;
        getline(ss, playerName, ',');
        getline(ss, password, ',');

        player->setPlayerName(playerName);
        player->setPassword(password);

        if (player->getPlayerName() == "" && player->getPassword() == "")
        {
            return true;
        }

        getline(ss, cell, ',');

        if (cell != "")
        {
            player->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Easy(0);
        }

        getline(ss, cell, ',');


        string timestampEasy;
        getline(ss, timestampEasy, ',');

        player->setTimestampEasy(timestampEasy);






        getline(ss, cell, ',');

        if (cell != "")
        {
            player->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Normal(0);
        }

        getline(ss, cell, ',');

        string timestampNormal;
        getline(ss, timestampNormal, ',');

        player->setTimestampNormal(timestampNormal);






        getline(ss, cell, ',');

        if (cell != "")
        {
            player->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Difficult(0);
        }

        getline(ss, cell, ',');

        string timestampDifficult;
        getline(ss, timestampDifficult, ',');

        player->setTimestampDifficult(timestampDifficult);



        int i = 0;

        while (getline(ss, cell, ','))
        {
            if (cell != "")
            {
                if (i % 2 == 0)
                {
                    player->setScores_Push(stoi(cell));

                    i++;
                }
                else if (i % 2 == 1)
                {
                    player->setGameDiffclutys_Push(cell);

                    i++;
                }
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

bool Filedata::Update_TopScore_RecentScore(User* user)
{
    if (user->getScores().size() < 20)
    {
        user->setScores_Push(user->getScore());
        user->setGameDiffclutys_Push(user->getGameDiffculty());
    }
    else
    {
        user->setScores_Push(user->getScore());
        user->setScores_Pop();

        user->setGameDiffclutys_Push(user->getGameDiffculty());
        user->setGameDiffclutys_Pop();
    }

    
    const string filename = "userdata.csv";//相对路径是相对于你的可执行文件所在的目录的

    ifstream file(filename);
    ofstream tempFile("temp_file.csv");//相对路径是相对于你的可执行文件所在的目录的

    if (!file.is_open() || !tempFile.is_open()) {
        //cerr << "Error opening files." << endl;
        logger->error("Error opening files.");
        logger->flush();
        return false;
    }

    string line;

    getline(file, line);//跳过第一行

    tempFile << line << endl;

    while (getline(file, line))
    {
        istringstream iss(line);
        string username, password, Maximum_score_easy, timestamp_easy, Maximum_score_normal, timestamp_normal, Maximum_score_diffcult, timestamp_diffcult, cell;


        getline(iss, username, ',');
        getline(iss, password, ',');

        getline(iss, Maximum_score_easy, ',');
        getline(iss, cell, ',');
        getline(iss, timestamp_easy, ',');

        getline(iss, Maximum_score_normal, ',');
        getline(iss, cell, ',');
        getline(iss, timestamp_normal, ',');

        getline(iss, Maximum_score_diffcult, ',');
        getline(iss, cell, ',');
        getline(iss, timestamp_diffcult, ',');


        if (username == user->getUsername()) // 在找到匹配行时，添加新数据
        {
            if (user->getGameDiffculty() == "EASY")
            {
                if (user->getScore() > user->getMaximum_Score_Easy())
                {
                    user->setMaximum_Score_Easy(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << user->getScore() << "," << "EASY" << "," << currenttime() << ",";

                    tempFile << Maximum_score_normal << "," << "NORAML" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFCULT" << "," << timestamp_diffcult << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORAML" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFCULT" << "," << timestamp_diffcult << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
            else if (user->getGameDiffculty() == "NORMAL")
            {
                if (user->getScore() > user->getMaximum_Score_Normal())
                {
                    user->setMaximum_Score_Normal(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << user->getScore() << "," << "NORAML" << "," << currenttime() << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFCULT" << "," << timestamp_diffcult << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORAML" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFCULT" << "," << timestamp_diffcult << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
            else if (user->getGameDiffculty() == "DIFFCULT")
            {
                if (user->getScore() > user->getMaximum_Score_Diffcult())
                {
                    user->setMaximum_Score_Diffcult(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORAML" << "," << timestamp_normal << ",";

                    tempFile << user->getScore() << "," << "DIFFCULT" << "," << currenttime() << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORAML" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFCULT" << "," << timestamp_diffcult << ",";

                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    while (!tempQueue.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
        }
        else
        {
            tempFile << line << endl;
        }
    }

    file.close();
    tempFile.close();

    // 删除原文件
    remove(filename.c_str());

    // 重命名临时文件为原文件
    rename("temp_file.csv", filename.c_str());//相对路径是相对于你的可执行文件所在的目录的

    return true;
}