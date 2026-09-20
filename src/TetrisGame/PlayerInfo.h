#pragma once
#include"../Common/Common.h"
#ifndef Player_H
#define Player_H

class PlayerInfo
{
public:

    PlayerInfo();

    // Getter and Setter functions for players
    static vector<shared_ptr<PlayerInfo>>& getPlayers();
    static void setPlayers(const vector<shared_ptr<PlayerInfo>>& newPlayers);

    static void addPlayer(shared_ptr<PlayerInfo> player);
    static void clearPlayers();

    // Getter and Setter functions for playername
    string getPlayerName() const;
    void setPlayerName(const string& playerName);

    // Getter and Setter functions for password
    string getPassword() const;
    void setPassword(const string& password);

    // Getter and Setter functions for scores
    vector<int> getScores() const;
    void setScores(const vector<int>& scores);
    void setScores_Push(const int& value);

    // Getter and Setter functions for game_diffclutys
    vector<string> getGameDifficulties() const;
    void setGameDifficulties(const vector<string>& gameDifficulties);
    void setGameDiffclutys_Push(const string& value);

    // Getter and Setter functions for Maximum_score_easy
    int getMaximum_Score_Easy() const;
    void setMaximum_Score_Easy(int maximumScoreEasy);

    // Getter and Setter functions for Maximum_score_normal
    int getMaximum_Score_Normal() const;
    void setMaximum_Score_Normal(int maximumScoreNormal);

    // Getter and Setter functions for Maximum_score_diffcult
    int getMaximum_Score_Difficult() const;
    void setMaximum_Score_Difficult(int maximumScoreDifficult);

    // Getter and Setter functions for timestamp_easy
    string getTimestampEasy() const;
    void setTimestampEasy(const string& timestampEasy);

    // Getter and Setter functions for timestamp_normal
    string getTimestampNormal() const;
    void setTimestampNormal(const string& timestampNormal);

    // Getter and Setter functions for timestamp_diffcult
    string getTimestampDifficult() const;
    void setTimestampDifficult(const string& timestampDifficult);

private:

    static std::vector<std::shared_ptr<PlayerInfo>> players;//定义静态成员变量,存储所有已注册用户的信息
    static std::mutex players_mutex;// 添加线程安全

    string playername;
    string password;
    //int Maximum_score;
    //string timestamp;
    vector<int> scores;
    vector<string> game_diffclutys;

    int Maximum_score_easy;
    int Maximum_score_normal;
    int Maximum_score_diffcult;

    string timestamp_easy;
    string timestamp_normal;
    string timestamp_diffcult;

};

bool cmp_easy(const shared_ptr<PlayerInfo> a, const shared_ptr<PlayerInfo> b);

bool cmp_normal(const shared_ptr<PlayerInfo> a, const shared_ptr<PlayerInfo> b);

bool cmp_diffcult(const shared_ptr<PlayerInfo> a, const shared_ptr<PlayerInfo> b);

#endif