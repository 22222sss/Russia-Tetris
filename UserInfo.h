#pragma once
#ifndef UserInfo_H
#define UserInfo_H

#include"Public.h"

class UserInfo
{

public:
    UserInfo(int fd);


public:
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
    int Maximum_score;
    queue<int> scores;
    string timestamp;
    string receivedata;

    double speed;

    std::chrono::steady_clock::time_point lastTriggerTime;
    std::chrono::steady_clock::time_point currentTime;

};

extern map<int, UserInfo*> g_playing_gamer;

#endif 