#include"UserInfo.h"
#include"Public.h"

UserInfo::UserInfo(int fd) :fd(fd), line(0), score(0)
{
    this->username = "";
    this->status = STATUS_NOTSTART;
    this->password = "";
    this->Maximum_score = 0;
    this->scores = {};
    this->timestamp = "";

    this->lastTriggerTime = std::chrono::steady_clock::now();

    memset(data, 0, sizeof(data));
    memset(color, -1, sizeof(color));
}