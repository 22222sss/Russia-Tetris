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

bool output(UserInfo* user, string s)
{
    int bytesSent = send(user->fd, s.c_str(), s.length(), 0);
    if (bytesSent == -1)
    {
        if (user->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore(user);
        }
        user->status = STATUS_OVER_QUIT;
        close(user->fd);
        //printf("Client[%d] send Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        logger->error("Client[{}] send Error: {} (errno: {})\n", user->fd, strerror(errno), errno);
        logger->flush();
        return false;
    }
    else if (bytesSent == 0)
    {
        if (user->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore(user);
        }
        // �ͻ��������ѹر�
        user->status = STATUS_OVER_QUIT;
        close(user->fd);
        return false;
    }
    return true;
}

bool moveTo(UserInfo* user, int row, int col) {
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

bool ChangeCurrentColor(UserInfo* user, int n)
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

bool outputText(UserInfo* user, int row, int col, int n, string s)
{
    if (user->status == STATUS_PLAYING)
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


    if (user->status == STATUS_PLAYING)
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

bool outputgrade(UserInfo* user, int row, int col, int n, string s, int grade)
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

bool clear(UserInfo* user)
{
    int i;
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    for (i = 1; i <= WINDOW_ROW_COUNT * 10; i++)
    {
        if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
            return false;
    }
    return true;
}

bool Update_TopScore_RecentScore(UserInfo* user)
{

    if (user->scores.size() < 20)
    {
        user->scores.push(user->score);
    }
    else
    {
        user->scores.push(user->score);
        user->scores.pop();
    }

    const string filename = "userdata.csv";

    ifstream file(filename);
    ofstream tempFile("temp_file.csv");

    if (!file.is_open() || !tempFile.is_open()) {
        //cerr << "Error opening files." << endl;
        logger->error("Error opening files.");
        logger->flush();
        return false;
    }

    string line;

    getline(file, line);//������һ��

    tempFile << line << endl;

    while (getline(file, line)) {
        istringstream iss(line);
        string username, password, Maximum_score, timestamp, cell;
        getline(iss, username, ',');
        getline(iss, password, ',');
        getline(iss, Maximum_score, ',');
        getline(iss, timestamp, ',');

        if (username == user->username) {
            // ���ҵ�ƥ����ʱ�����������
            if (user->score > user->Maximum_score)
            {
                user->Maximum_score = user->score;

                tempFile << username << "," << password << "," << user->score << "," << currenttime() << ",";

                queue<int> tempQueue = user->scores;
                while (!tempQueue.empty()) {
                    tempFile << tempQueue.front() << ",";
                    tempQueue.pop();
                }

                tempFile << endl;
            }
            else
            {
                tempFile << username << "," << password << "," << Maximum_score << "," << timestamp << ",";

                queue<int> tempQueue = user->scores;
                while (!tempQueue.empty()) {
                    tempFile << tempQueue.front() << ",";
                    tempQueue.pop();
                }

                tempFile << endl;
            }
        }
        else {
            tempFile << line << endl;
        }
    }

    file.close();
    tempFile.close();

    // ɾ��ԭ�ļ�
    remove(filename.c_str());

    // ��������ʱ�ļ�Ϊԭ�ļ�
    rename("temp_file.csv", filename.c_str());

    return true;
}
