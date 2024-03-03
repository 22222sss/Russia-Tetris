#pragma once
#include"../Common/Common.h"
#ifndef Player_H
#define Player_H

class PlayerInfo
{
public:
    PlayerInfo();

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

// 全局变量，存储所有用户数据
extern vector<PlayerInfo*> players;

bool cmp_easy(const PlayerInfo* a, const PlayerInfo* b);

bool cmp_normal(const PlayerInfo* a, const PlayerInfo* b);

bool cmp_diffcult(const PlayerInfo* a, const PlayerInfo* b);

#endif