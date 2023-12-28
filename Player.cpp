#include"Player.h"

Player::Player()
{
    this->playername = "";
    this->password = "";
    this->Maximum_score = 0;
    this->scores = {};
    this->timestamp = "";
}

bool cmp(const Player* a, const Player* b)
{
    return a->Maximum_score > b->Maximum_score;
}
