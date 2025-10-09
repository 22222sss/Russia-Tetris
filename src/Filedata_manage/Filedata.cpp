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
    getline(file, line); // ��ȡ������

    while (getline(file, line))
    {
        // �Ƴ�Windows���з�\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;

        getline(ss, cell, ',');//��ȡ�û���

        if (cell == user->getUsername())
        {
            getline(ss, cell, ',');//��ȡ����

            // ����EASY�Ѷ�����
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // ����NORMAL�Ѷ�����
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // ����DIFFICULT�Ѷ�����
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // ��ȡ����ĳɼ�
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

                        // ֻ���������20����¼
                        if (count >= 20) break;
                    }
                }
            }
            file.close();

            if (temp.empty())
            {
                temp.push_back("1"); // ��ʾ�û����ڵ�û�гɼ���¼
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
    getline(file, line); // ��ȡ������

    while (getline(file, line))
    {
        // �Ƴ�Windows���з�\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;
        PlayerInfo* gamer = new PlayerInfo;

        string playerName, Password;

        getline(ss, playerName, ',');//��ȡ�û���
        gamer->setPlayerName(playerName);

        getline(ss, Password, ',');//��ȡ����
        gamer->setPassword(Password);

        // ��ȡEASY�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Easy(0);
        }

        // ����EASY�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡEASY�Ѷ�ʱ���
        string TimestampEasy;
        getline(ss, TimestampEasy, ',');
        gamer->setTimestampEasy(TimestampEasy);

        // ��ȡNORMAL�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Normal(0);
        }

        // ����NORMAL�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡNORMAL�Ѷ�ʱ���
        string TimestampNormal;
        getline(ss, TimestampNormal, ',');
        gamer->setTimestampNormal(TimestampNormal);

        // ��ȡDIFFICULT�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Difficult(0);
        }

        // ����DIFFICULT�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡDIFFICULT�Ѷ�ʱ���
        string TimestampDifficult;
        getline(ss, TimestampDifficult, ',');
        gamer->setTimestampDifficult(TimestampDifficult);

        // ��ȡ����ĳɼ���¼
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

    // ����ļ��Ƿ�Ϊ�գ����������ӱ�����
    struct stat stat_buf;
    if (stat("userdata.csv", &stat_buf) == 0 && stat_buf.st_size == 0)
    {
        file << "Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
            << "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
            << "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores\n";
    }

    // д���û�����
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
    getline(file, line); // ��ȡ������

    while (getline(file, line))
    {
        // �Ƴ�Windows���з�\r
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

        // ��ȡEASY�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Easy(0);
        }

        // ����EASY�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡEASY�Ѷ�ʱ���
        string timestampEasy;
        getline(ss, timestampEasy, ',');
        player->setTimestampEasy(timestampEasy);

        // ��ȡNORMAL�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Normal(0);
        }

        // ����NORMAL�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡNORMAL�Ѷ�ʱ���
        string timestampNormal;
        getline(ss, timestampNormal, ',');
        player->setTimestampNormal(timestampNormal);

        // ��ȡDIFFICULT�Ѷ���߷�
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Difficult(0);
        }

        // ����DIFFICULT�Ѷ��ַ���
        getline(ss, cell, ',');

        // ��ȡDIFFICULT�Ѷ�ʱ���
        string timestampDifficult;
        getline(ss, timestampDifficult, ',');
        player->setTimestampDifficult(timestampDifficult);

        // ��ȡ����ĳɼ���¼
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
    // ��ӿ�ָ����
    if (user == nullptr) {
        logger->error("User pointer is null in Update_TopScore_RecentScore");
        logger->flush();
        return false;
    }

    // ��Ӷ��û��������еļ��
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

    // ����ļ������Լ��
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

    // ������һ��
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

        // ��ӽ������
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

                    // ��Ӷ��пռ��
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

                    // ��Ӷ��пռ��
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

                    // ��Ӷ��пռ��
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

                    // ��Ӷ��пռ��
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

                    // ��Ӷ��пռ��
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

                    // ��Ӷ��пռ��
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

    // ɾ��ԭ�ļ�ǰ�����ʱ�ļ��Ƿ�ɹ�����
    ifstream tempCheck("temp_file.csv");
    if (!tempCheck.is_open()) {
        logger->error("Temporary file was not created successfully.");
        logger->flush();
        return false;
    }
    tempCheck.close();

    // ɾ��ԭ�ļ�
    if (remove(filename.c_str()) != 0) {
        logger->error("Failed to delete original file: {}", filename);
        logger->flush();
        return false;
    }

    // ��������ʱ�ļ�Ϊԭ�ļ�
    if (rename("temp_file.csv", filename.c_str()) != 0) {
        logger->error("Failed to rename temporary file.");
        logger->flush();
        return false;
    }

    return true;
}