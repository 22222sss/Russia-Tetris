#include"PlayerInfo.h"

PlayerInfo::PlayerInfo()
{
    this->playername = "";
    this->password = "";
    this->scores = {};
    this->game_diffclutys = {};
}

string PlayerInfo::getPlayerName() const
{
    return this->playername;
}

void PlayerInfo::setPlayerName(const string& playerName)
{
    this->playername = playerName;
}

string PlayerInfo::getPassword() const
{
    return this->password;
}

void PlayerInfo::setPassword(const string& password)
{
    this->password = password;
}

vector<int> PlayerInfo::getScores() const
{
    return this->scores;
}

void PlayerInfo::setScores(const vector<int>& scores)
{
    this->scores = scores;
}

void PlayerInfo::setScores_Push(const int& value)
{
    this->scores.push_back(value);
}

vector<string> PlayerInfo::getGameDifficulties() const
{
    return this->game_diffclutys;
}

void PlayerInfo::setGameDifficulties(const vector<string>& gameDifficulties)
{
    this->game_diffclutys = gameDifficulties;
}

void PlayerInfo::setGameDiffclutys_Push(const string& value)
{
    this->game_diffclutys.push_back(value);
}

int PlayerInfo::getMaximum_Score_Easy() const
{
    return this->Maximum_score_easy;
}

void PlayerInfo::setMaximum_Score_Easy(int maximumScoreEasy)
{
    this->Maximum_score_easy = maximumScoreEasy;
}

int PlayerInfo::getMaximum_Score_Normal() const
{
    return this->Maximum_score_normal;
}

void PlayerInfo::setMaximum_Score_Normal(int maximumScoreNormal)
{
    this->Maximum_score_normal = maximumScoreNormal;
}

int PlayerInfo::getMaximum_Score_Difficult() const
{
    return this->Maximum_score_diffcult;
}

void PlayerInfo::setMaximum_Score_Difficult(int maximumScoreDifficult)
{
    this->Maximum_score_diffcult = maximumScoreDifficult;
}

string PlayerInfo::getTimestampEasy() const
{
    return this->timestamp_easy;
}

void PlayerInfo::setTimestampEasy(const string& timestampEasy)
{
    this->timestamp_easy = timestampEasy;
}

string PlayerInfo::getTimestampNormal() const
{
    return this->timestamp_normal;
}

void PlayerInfo::setTimestampNormal(const string& timestampNormal)
{
    this->timestamp_normal = timestampNormal;
}

string PlayerInfo::getTimestampDifficult() const
{
    return this->timestamp_diffcult;
}

void PlayerInfo::setTimestampDifficult(const string& timestampDifficult)
{
    this->timestamp_diffcult = timestampDifficult;
}

bool cmp_easy(const PlayerInfo* a, const PlayerInfo* b)
{
    return a->getMaximum_Score_Easy() > b->getMaximum_Score_Easy();
}

bool cmp_normal(const PlayerInfo* a, const PlayerInfo* b)
{
    return a->getMaximum_Score_Normal() > b->getMaximum_Score_Normal();
}

bool cmp_diffcult(const PlayerInfo* a, const PlayerInfo* b)
{
    return a->getMaximum_Score_Difficult() > b->getMaximum_Score_Difficult();
}