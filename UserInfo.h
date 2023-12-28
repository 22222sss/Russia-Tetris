
#pragma once
#ifndef UserInfo_H
#define UserInfo_H

#include"Tetris.h"

class UserInfo {
public:
    UserInfo(int fd);

    bool output(string s);

    bool moveTo(int row, int col);

    bool ChangeCurrentColor(int n);

    bool outputgrade(int row, int col, int n, string s, int grade);

    bool outputText(int row, int col, int n, string s);

    bool InitInterface();

    //画出方块
    bool DrawBlock(int shape, int form, int row, int col);//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列

    //空格覆盖
    //无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
    //而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
    //在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

    bool DrawSpace(int shape, int form, int row, int col);

    //其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
    //所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
    //合法性判断

    bool IsLegal(int shape, int form, int row, int col);

    //判断得分与结束

    // 判断得分
    //从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
    //因为被下移的行并没有进行判断，可能还存在满行。

    int Is_Increase_Score();

    bool UpdateCurrentScore();

    bool clear();

    //判断结束
    //直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
    //游戏结束后询问玩家是否再来一局。

    bool IsOver();
    
    //展示结束界面
    bool showover();

    bool showInitMenu();

    // 注册新用户
    bool registerUser(int epollfd);

    //返回初始菜单界面
    int returnToInitMenu(int epollfd);

    //接收用户信息
    int ReceiveData(int epollfd);

    // 用户登录
    bool loadUser(int epollfd);

    // 写入用户数据文件，保存更新后的用户数据
    void saveUserData();

    bool InitGameFace();

    //展示登录界面
    bool showLoadMenu();

    // 查看本人最近20次比赛的分数
    bool showRecentScores(int epollfd);

    // 查看全服top成绩
    bool showTopScores(int epollfd);

    //展示游戏难度
    bool showGameDifficulty(int epollfd);

    //返回登录界面
    int returnToLoadMenu(int i,int epollfd);

    //更新用户最高分
    bool Update_TopScore_RecentScore();

    //重置用户信息
    void resetUserInfo();

    //接收注册用户名
    int receive_username_register(int epollfd);

    //接收注册密码
    int receive_password_register(int epollfd);
    
    //接收登录用户名
    int receive_username_load(int epollfd);

    //接收登录密码
    int receive_password_load(int epollfd);

    //处理用户登录成功后的逻辑
    int loginUser(int epollfd);

    //选择游戏难度
    int select_game_difficulty(int epollfd);

    // 定义处理用户逻辑的函数
    void processBlockDown();

    //处理定时触发逻辑
    void handleTimedUserLogic();


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


    //static std::chrono::steady_clock::time_point& lastTriggerTime;
    std::chrono::steady_clock::time_point lastTriggerTime;
    std::chrono::steady_clock::time_point currentTime;

};

extern map<int, UserInfo*> g_playing_gamer;

#endif 



