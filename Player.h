#pragma once
#include"Tetris.h"
#ifndef Player_H
#define Player_H

class Player
{
public:
    Player();

public:

    string playername;
    string password;
    int Maximum_score;
    string timestamp;
    vector<int> scores;
};

// 全局变量，存储所有用户数据
extern vector<Player*> players;

bool cmp(const Player* a, const Player* b);
#endif