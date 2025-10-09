#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

User::User(int fd) :fd(fd), line(0), score(0)
{
    this->username = "";
    this->status = STATUS_NOTSTART;
    this->password = "";
    this->scores = {};

    this->lastTriggerTime = std::chrono::steady_clock::now();

    memset(data, 0, sizeof(data));
    memset(color, -1, sizeof(color));
}

//重置用户信息
void User::resetUserInfo()
{
    this->line = 0;
    this->score = 0;
    this->username = "";
    this->status = STATUS_NOTSTART;
    this->password = "";
    this->scores = {};
    this->game_diffclutys = {};
    memset(this->data, 0, sizeof(this->data));
    memset(this->color, -1, sizeof(this->color));
}

map<int, User*>& User::getUsers()
{
    static map<int, User*> users;  // 定义私有局部静态变量,存储所有已连接用户的信息

    return users;
}

void User::setUsers(const map<int, User*>& newUsers) 
{
    User::getUsers() = newUsers;
}

void User::initUserInfo()
{
    for (const auto& player : PlayerInfo::getPlayers())
    {
        if (player->getPlayerName() == this->getUsername() && player->getPassword() == this->getPassword())
        {
            
            this->setMaximum_Score_Easy(player->getMaximum_Score_Easy());

            for (const auto& score : player->getScores())
            {
                this->setScores_Push(score);
            }

            for (const auto& diffculty : player->getGameDifficulties())
            {
                this->setGameDiffclutys_Push(diffculty);
            }

            break;

        }
    }
}

// Getter and setter functions for Fd

int User::getFd() const
{
    return this->fd;
}

void User::setFd(int value)
{
    this->fd = value;
}

// Getter and setter functions for line

int User::getLine() const
{
    return this->line;
}

void User::setLine(int value)
{
    this->line = value;
}

// Getter and setter functions for score

int User::getScore() const
{
    return this->score;
}

void User::setScore(int value)
{
    this->score = value;
}

// Getter and setter functions for status

int User::getStatus() const
{
    return this->status;
}

void User::setStatus(int value)
{
    this->status = value;
}

// Getter and setter functions for shape

int User::getShape() const
{
    return this->shape;
}

void User::setShape(int value)
{
    this->shape = value;
}

// Getter and setter functions for form

int User::getForm() const
{
    return this->form;
}

void User::setForm(int value)
{
    this->form = value;
}

// Getter and setter functions for nextShape

int User::getNextShape() const
{
    return this->nextShape;
}

void User::setNextShape(int value)
{
    this->nextShape = value;
}

// Getter and setter functions for nextForm

int User::getNextForm() const
{
    return this->nextForm;
}

void User::setNextForm(int value)
{
    this->nextForm = value;
}

// Getter and setter functions for row

int User::getRow() const
{
    return this->row;
}

void User::setRow(int value)
{
    this->row = value;
}

// Getter and setter functions for col

int User::getCol() const
{
    return this->col;
}

void User::setCol(int value)
{
    this->col = value;
}

// Getter and setter functions for data

int User::getData(int i, int j) const
{
    return this->data[i][j];
}

void User::setData(int i, int j, int value)
{
    this->data[i][j] = value;
}

// Getter and setter functions for color

int User::getColor(int i, int j) const
{
    return this->color[i][j];
}

void User::setColor(int i, int j, int value)
{
    this->color[i][j] = value;
}

// Getter and setter functions for username

string User::getUsername() const
{
    return this->username;
}

void User::setUsername(const string& value)
{
    this->username = value;
}

// Getter and setter functions for password

string User::getPassword() const
{
    return this->password;
}

void User::setPassword(const string& value)
{
    this->password = value;
}

// Getter and setter functions for Maximum_score_easy

int User::getMaximum_Score_Easy() const
{
    return this->Maximum_score_easy;
}

void User::setMaximum_Score_Easy(int value)
{
    this->Maximum_score_easy = value;
}

// Getter and setter functions for Maximum_score_normal

int User::getMaximum_Score_Normal() const
{
    return this->Maximum_score_normal;
}

void User::setMaximum_Score_Normal(int value)
{
    this->Maximum_score_normal = value;
}

// Getter and setter functions for Maximum_score_diffcult

int User::getMaximum_Score_Diffcult() const
{
    return this->Maximum_score_diffcult;
}

void User::setMaximum_Score_Diffcult(int value)
{
    this->Maximum_score_diffcult = value;
}

// Getter and setter functions for scores

queue<int> User::getScores() const
{
    return this->scores;
}

void User::setScores(const queue<int>& value)
{
    this->scores = value;
}

void User::setScores_Push(const int& value)
{
    this->scores.push(value);
}

void User::setScores_Pop()
{
    this->scores.pop();
}

// Getter and setter functions for game_diffclutys

queue<string> User::getGameDiffclutys() const
{
    return this->game_diffclutys;
}

void User::setGameDiffclutys(const queue<string>& value)
{
    this->game_diffclutys = value;
}


void User::setGameDiffclutys_Push(const string& value)
{
    this->game_diffclutys.push(value);
}

void User::setGameDiffclutys_Pop()
{
    this->game_diffclutys.pop();
}

// Getter and setter functions for timestamp_easy

string User::getTimestamp_Easy() const
{
    return this->timestamp_easy;
}

void User::setTimestamp_Easy(const string& value)
{
    this->timestamp_easy = value;
}

// Getter and setter functions for timestamp_normal

string User::getTimestamp_Normal() const
{
    return this->timestamp_normal;
}

void User::setTimestamp_Normal(const string& value)
{
    this->timestamp_normal = value;
}

// Getter and setter functions for timestamp_diffcult

string User::getTimestamp_Diffcult() const
{
    return this->timestamp_diffcult;
}

void User::setTimestamp_Diffcult(const string& value)
{
    this->timestamp_diffcult = value;
}

// Getter and setter functions for receivedata

string User::getReceivedata() const
{
    return this->receivedata;
}

void User::setReceivedata(const string& value)
{
    this->receivedata = value;
}

// Getter and setter functions for speed

double User::getSpeed() const
{
    return this->speed;
}

void User::setSpeed(double value)
{
    this->speed = value;
}

// Getter and setter functions for lastTriggerTime

std::chrono::steady_clock::time_point User::getLastTriggerTime() const
{
    return this->lastTriggerTime;
}

void User::setLastTriggerTime(const std::chrono::steady_clock::time_point& value)
{
    this->lastTriggerTime = value;
}

// Getter and setter functions for currentTime

std::chrono::steady_clock::time_point User::getCurrentTime() const
{
    return this->currentTime;
}

void User::setCurrentTime(const std::chrono::steady_clock::time_point& value)
{
    this->currentTime = value;
}

// Getter and setter functions for game_diffculty

string User::getGameDiffculty() const
{
    return this->game_diffculty;
}

void User::setGameDiffculty(const string& value)
{
    this->game_diffculty = value;
}