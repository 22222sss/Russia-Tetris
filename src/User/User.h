#pragma once
#ifndef User_H
#define User_H

#include"../Common/Common.h"

class User
{

public:
    User(int fd);

    // Getter and setter functions for users
    static map<int, User*>& getUsers();

    // 基础 Setter
    static void setUsers(const map<int, User*>& newUsers);

    //重置用户信息
    void resetUserInfo();

    void initUserInfo();

    int getFd() const;
    void setFd(int value);

    // Getter and setter functions for line
    int getLine() const;
    void setLine(int value);

    // Getter and setter functions for score
    int getScore() const;
    void setScore(int value);

    // Getter and setter functions for status
    int getStatus() const;
    void setStatus(int value);

    // Getter and setter functions for shape
    int getShape() const;
    void setShape(int value);

    // Getter and setter functions for form
    int getForm() const;
    void setForm(int value);

    // Getter and setter functions for nextShape
    int getNextShape() const;
    void setNextShape(int value);

    // Getter and setter functions for nextForm
    int getNextForm() const;
    void setNextForm(int value);

    // Getter and setter functions for row
    int getRow() const;
    void setRow(int value);

    // Getter and setter functions for col
    int getCol() const;
    void setCol(int value);

    // Getter and setter functions for data
    int getData(int i, int j) const;
    void setData(int i, int j, int value);

    // Getter and setter functions for color
    int getColor(int i, int j) const;
    void setColor(int i, int j, int value);

    // Getter and setter functions for username
    string getUsername() const;
    void setUsername(const string& value);

    // Getter and setter functions for password
    string getPassword() const;
    void setPassword(const string& value);

    // Getter and setter functions for Maximum_score_easy
    int getMaximum_Score_Easy() const;
    void setMaximum_Score_Easy(int value);

    // Getter and setter functions for Maximum_score_normal
    int getMaximum_Score_Normal() const;
    void setMaximum_Score_Normal(int value);

    // Getter and setter functions for Maximum_score_diffcult
    int getMaximum_Score_Diffcult() const;
    void setMaximum_Score_Diffcult(int value);

    // Getter and setter functions for scores
    queue<int> getScores() const;
    void setScores(const queue<int>& value);
    void setScores_Push(const int& value);
    void setScores_Pop();


    // Getter and setter functions for game_diffclutys
    queue<string> getGameDiffclutys() const;
    void setGameDiffclutys(const queue<string>& value);
    void setGameDiffclutys_Push(const string& value);
    void setGameDiffclutys_Pop();

    // Getter and setter functions for timestamp_easy
    string getTimestamp_Easy() const;
    void setTimestamp_Easy(const string& value);

    // Getter and setter functions for timestamp_normal
    string getTimestamp_Normal() const;
    void setTimestamp_Normal(const string& value);

    // Getter and setter functions for timestamp_diffcult

    string getTimestamp_Diffcult() const;
    void setTimestamp_Diffcult(const string& value);

    // Getter and setter functions for receivedata
    string getReceivedata() const;
    void setReceivedata(const string& value);

    // Getter and setter functions for speed
    double getSpeed() const;
    void setSpeed(double value);

    // Getter and setter functions for lastTriggerTime
    std::chrono::steady_clock::time_point getLastTriggerTime() const;
    void setLastTriggerTime(const std::chrono::steady_clock::time_point& value);

    // Getter and setter functions for currentTime
    std::chrono::steady_clock::time_point getCurrentTime() const;
    void setCurrentTime(const std::chrono::steady_clock::time_point& value);

    // Getter and setter functions for game_diffculty
    string getGameDiffculty() const;
    void setGameDiffculty(const string& value);

private:
    int fd;
    int line;
    int score;
    int status;
    int shape;
    int form;
    int nextShape;
    int nextForm;
    int row;
    int col;
    int data[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];
    int color[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];

    string username;
    string password;
    int Maximum_score_easy;
    int Maximum_score_normal;
    int Maximum_score_diffcult;
    queue<int> scores;

    queue<string> game_diffclutys;
    string timestamp_easy;
    string timestamp_normal;
    string timestamp_diffcult;

    string receivedata;

    double speed;

    std::chrono::steady_clock::time_point lastTriggerTime;
    std::chrono::steady_clock::time_point currentTime;

    string game_diffculty;

};



#endif 