#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

#include <sys/stat.h>
#include <algorithm>

vector<string> Filedata::Read_recent_grades(User* user)
{
    vector<string> temp = {};
    ifstream file("userdata.csv");

    if (!file.is_open())
    {
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
        logger->flush();
        temp.push_back("-1");
        return temp;
    }

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line))
    {
        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;

        getline(ss, cell, ',');//读取用户名

        if (cell == user->getUsername())
        {
            getline(ss, cell, ',');//读取密码

            // 跳过EASY难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 跳过NORMAL难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 跳过DIFFICULT难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 读取最近的成绩
            int count = 0;
            while (getline(ss, cell, ','))
            {
                if (cell.empty()) continue;

                string score = cell;
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        string outcome = score + "  " + difficulty;
                        temp.push_back(outcome);
                        count++;

                        // 只保留最近的20条记录
                        if (count >= 20) break;
                    }
                }
            }
            file.close();

            if (temp.empty())
            {
                temp.push_back("1"); // 表示用户存在但没有成绩记录
            }

            return temp;
        }
    }

    file.close();
    return temp;
}

vector<PlayerInfo*> Filedata::Read_AllpalyerInfo(ifstream& file)
{
    vector<PlayerInfo*> show = {};

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line))
    {
        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;
        PlayerInfo* gamer = new PlayerInfo;

        string playerName, Password;

        getline(ss, playerName, ',');//读取用户名
        gamer->setPlayerName(playerName);

        getline(ss, Password, ',');//读取密码
        gamer->setPassword(Password);

        // 读取EASY难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Easy(0);
        }

        // 跳过EASY难度字符串
        getline(ss, cell, ',');

        // 读取EASY难度时间戳
        string TimestampEasy;
        getline(ss, TimestampEasy, ',');
        gamer->setTimestampEasy(TimestampEasy);

        // 读取NORMAL难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Normal(0);
        }

        // 跳过NORMAL难度字符串
        getline(ss, cell, ',');

        // 读取NORMAL难度时间戳
        string TimestampNormal;
        getline(ss, TimestampNormal, ',');
        gamer->setTimestampNormal(TimestampNormal);

        // 读取DIFFICULT难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Difficult(0);
        }

        // 跳过DIFFICULT难度字符串
        getline(ss, cell, ',');

        // 读取DIFFICULT难度时间戳
        string TimestampDifficult;
        getline(ss, TimestampDifficult, ',');
        gamer->setTimestampDifficult(TimestampDifficult);

        // 读取最近的成绩记录
        while (getline(ss, cell, ','))
        {
            if (!cell.empty() && isNumber(cell))
            {
                int score = stoi(cell);
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        gamer->setScores_Push(score);
                        gamer->setGameDiffclutys_Push(difficulty);
                    }
                }
            }
        }

        show.push_back(gamer);
    }

    file.close();
    return show;
}

bool Filedata::saveNewUserData(User* user)
{
    ofstream file("userdata.csv", std::ios::app);

    if (!file.is_open())
    {
        logger->error("Unable to open file or file opening failed! Error message: {}\n", std::strerror(errno));
        logger->flush();
        return false;
    }

    // 检查文件是否为空，如果是则添加标题行
    struct stat stat_buf;
    if (stat("userdata.csv", &stat_buf) == 0 && stat_buf.st_size == 0)
    {
        file << "Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
            << "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
            << "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores\n";
    }

    // 写入用户数据
    file << user->getUsername() << "," << user->getPassword() << ","
        << "0" << "," << "EASY" << "," << "" << ","
        << "0" << "," << "NORMAL" << "," << "" << ","
        << "0" << "," << "DIFFICULT" << "," << "" << ",\n";

    file.close();
    return true;
}

bool Filedata::loadPlayerData()
{
    PlayerInfo::setPlayers({});

    ifstream file("userdata.csv");
    if (!file.is_open())
    {
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
        logger->flush();
        return false;
    }

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line))
    {
        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        if (line.empty()) continue;

        istringstream ss(line);
        string cell;

        PlayerInfo* player = new PlayerInfo;
        string playerName, password;

        getline(ss, playerName, ',');
        getline(ss, password, ',');

        player->setPlayerName(playerName);
        player->setPassword(password);

        // 读取EASY难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Easy(0);
        }

        // 跳过EASY难度字符串
        getline(ss, cell, ',');

        // 读取EASY难度时间戳
        string timestampEasy;
        getline(ss, timestampEasy, ',');
        player->setTimestampEasy(timestampEasy);

        // 读取NORMAL难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Normal(0);
        }

        // 跳过NORMAL难度字符串
        getline(ss, cell, ',');

        // 读取NORMAL难度时间戳
        string timestampNormal;
        getline(ss, timestampNormal, ',');
        player->setTimestampNormal(timestampNormal);

        // 读取DIFFICULT难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Difficult(0);
        }

        // 跳过DIFFICULT难度字符串
        getline(ss, cell, ',');

        // 读取DIFFICULT难度时间戳
        string timestampDifficult;
        getline(ss, timestampDifficult, ',');
        player->setTimestampDifficult(timestampDifficult);

        // 读取最近的成绩记录
        while (getline(ss, cell, ','))
        {
            if (!cell.empty() && isNumber(cell))
            {
                int score = stoi(cell);
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        player->setScores_Push(score);
                        player->setGameDiffclutys_Push(difficulty);
                    }
                }
            }
        }

        PlayerInfo::getPlayers().push_back(player);
    }

    file.close();
    return true;
}

bool Filedata::Update_TopScore_RecentScore(User* user)
{
    // 添加空指针检查
    if (user == nullptr) {
        logger->error("User pointer is null in Update_TopScore_RecentScore");
        logger->flush();
        return false;
    }

    // 添加对用户分数队列的检查
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

    const string filename = "userdata.csv";

    // 添加文件存在性检查
    if (access(filename.c_str(), F_OK) == -1) {
        logger->error("File does not exist: {}", filename);
        logger->flush();
        return false;
    }

    ifstream file(filename);
    ofstream tempFile("temp_file.csv");

    if (!file.is_open() || !tempFile.is_open()) {
        logger->error("Error opening files.");
        logger->flush();
        return false;
    }

    string line;

    // 跳过第一行
    if (!getline(file, line)) {
        logger->error("Failed to read header from file.");
        logger->flush();
        file.close();
        tempFile.close();
        return false;
    }

    tempFile << line << endl;

    while (getline(file, line))
    {
        istringstream iss(line);
        string username, password, Maximum_score_easy, timestamp_easy, Maximum_score_normal, timestamp_normal, Maximum_score_diffcult, timestamp_diffcult, cell;

        // 添加解析检查
        if (!getline(iss, username, ',') ||
            !getline(iss, password, ',') ||
            !getline(iss, Maximum_score_easy, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_easy, ',') ||
            !getline(iss, Maximum_score_normal, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_normal, ',') ||
            !getline(iss, Maximum_score_diffcult, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_diffcult, ',')) {

            logger->warn("Skipping malformed line: {}", line);
            continue;
        }

        if (username == user->getUsername())
        {
            if (user->getGameDiffculty() == "EASY")
            {
                if (user->getScore() > user->getMaximum_Score_Easy())
                {
                    user->setMaximum_Score_Easy(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << user->getScore() << "," << "EASY" << "," << currenttime() << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
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

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
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

                    tempFile << user->getScore() << "," << "NORMAL" << "," << currenttime() << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
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

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
            else if (user->getGameDiffculty() == "DIFFICULT")
            {
                if (user->getScore() > user->getMaximum_Score_Diffcult())
                {
                    user->setMaximum_Score_Diffcult(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << user->getScore() << "," << "DIFFICULT" << "," << currenttime() << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
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

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
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

    // 删除原文件前检查临时文件是否成功创建
    ifstream tempCheck("temp_file.csv");
    if (!tempCheck.is_open()) {
        logger->error("Temporary file was not created successfully.");
        logger->flush();
        return false;
    }
    tempCheck.close();

    // 删除原文件
    if (remove(filename.c_str()) != 0) {
        logger->error("Failed to delete original file: {}", filename);
        logger->flush();
        return false;
    }

    // 重命名临时文件为原文件
    if (rename("temp_file.csv", filename.c_str()) != 0) {
        logger->error("Failed to rename temporary file.");
        logger->flush();
        return false;
    }

    return true;
}