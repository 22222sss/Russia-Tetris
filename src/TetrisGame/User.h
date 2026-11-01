#pragma once
#ifndef User_H
#define User_H

#include"../Common/Common.h"
#include <functional>
#include <cstdint>

class User
{

public:
    User(int fd);

    // Getter and setter functions for users

    static bool addUser(int fd, std::shared_ptr<User> user);

    static bool removeUser(int fd);

    static std::shared_ptr<User> getUser(int fd);

    // 在锁内遍历所有用户，避免复制整个 map（高并发性能优化）
    // 注意：回调内不要调用 removeUser（会死锁），需要删除的 fd 应在回调外处理
    static void forEachUser(const std::function<void(int, std::shared_ptr<User>&)>& func);

    //重置用户信息
    void resetUserInfo();

    void initUserInfo();

    int getFd() const;
    void setFd(int value);

    // 连接来源 IPv4（accept 时写入，主机字节序；0 表示未知）。按 IP 兜底用，
    // 见 TetrisGame/IpThrottle.h。存 4 字节而不是字符串：结构体有 2KB 预算。
    std::uint32_t getPeerIp() const;
    void setPeerIp(std::uint32_t value);

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

    // 发送缓冲：非阻塞 socket 上一次 send 可能写不完，剩余部分留在这里下次重发
    std::string& getSendBuffer();
    std::mutex& getSendMutex();
    bool hasPendingOutput() const;
    void setPendingOutput(bool value);

private:
    int fd;
    std::uint32_t peerIp = 0;   // 0 = 未知来源（没有 peer 地址时不参与按 IP 限流）
    int line;
    int score;
    int status;
    int shape;
    int form;
    int nextShape;
    int nextForm;
    int row;
    int col;
    // 棋盘只存两种值：data = 0/1（有无方块），color = -1（空）/ 0~6（方块颜色）。
    // 用 int 时这两个数组占 24*30*4*2 = 5760 字节，是 sizeof(User) 的 91%；
    // 10 万连接光用户对象就要 ~630MB。改用 int8_t（有符号！color 的 -1 是哨兵，
    // 无符号会变成 255）后棋盘只占 1440 字节，10 万连接降到 ~200MB。
    // 取值由 Game.cpp 保证在上述范围，热路径上不做额外边界检查。
    int8_t data[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];
    int8_t color[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];

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

    string sendbuf;                          // 待发数据（非阻塞 socket 上 send 可能只写出去一部分）
    std::mutex sendmutex;                    // 保护 sendbuf：异步回调线程也会调用 output
    std::atomic<bool> pendingoutput{false};  // 供 100ms 定时器快速判断是否需要补发

    double speed;

    std::chrono::steady_clock::time_point lastTriggerTime;
    std::chrono::steady_clock::time_point currentTime;

    string game_diffculty;

    static std::map<int, std::shared_ptr<User>> users;
    static std::mutex users_mutex; // 添加互斥锁
};

// 每个连接一个 User，10 万连接时每多 1KB 就是 100MB。棋盘改成 int8_t 后约 2KB；
// 这条断言防止以后有人把 data/color 改回 int 或往结构体里塞大字段而悄悄涨回去。
static_assert(sizeof(User) <= 2048, "User 结构体溢出了 2KB 预算（10 万连接约 200MB）");



#endif 