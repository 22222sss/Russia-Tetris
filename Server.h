#pragma once
#ifndef Server_H
#define Server_H

#include"UserInfo.h"
//#include"Game.h"

class Server
{
public:
	Server();

    //展示初始界面
    bool showInitMenu(UserInfo* user);

    // 注册新用户
    bool registerUser(UserInfo* user,int epollfd);

    //接收用户信息
    int ReceiveData(UserInfo* user, int epollfd);

    //返回初始菜单界面
    int returnToInitMenu(UserInfo* user,int epollfd);

    // 用户登录
    bool loadUser(UserInfo* user,int epollfd);

    // 写入用户数据文件，保存更新后的用户数据
    void saveUserData(UserInfo* user);

    //展示登录界面
    bool showLoadMenu(UserInfo* user);

    // 查看本人最近20次比赛的分数
    bool showRecentScores(UserInfo* user,int epollfd);

    // 查看全服top成绩
    bool showTopScores(UserInfo* user,int epollfd);

    //展示游戏难度
    bool showGameDifficulty(UserInfo* user,int epollfd);

    //返回登录界面
    int returnToLoadMenu(UserInfo* user,int i, int epollfd);

    //接收注册用户名
    int receive_username_register(UserInfo* user,int epollfd);

    //接收注册密码
    int receive_password_register(UserInfo* user,int epollfd);

    //接收登录用户名
    int receive_username_load(UserInfo* user,int epollfd);

    //接收登录密码
    int receive_password_load(UserInfo* user,int epollfd);

    //处理用户登录成功后的逻辑
    int loginUser(UserInfo* user,int epollfd);

    //STATUS_NOTSTART
    bool process_STATUS_NOTSTART(UserInfo* user, int epollfd);

    //STATUS_RECEIVE_USERNAME_REGISTER
    bool process_STATUS_RECEIVE_USERNAME_REGISTER(UserInfo* user, int epollfd);

    //STATUS_RECEIVE_PASSWORD_REGISTER
    bool process_STATUS_RECEIVE_PASSWORD_REGISTER(UserInfo* user, int epollfd);

    //STATUS_RECEIVE_USERNAME_LOAD
    bool process_STATUS_RECEIVE_USERNAME_LOAD(UserInfo* user, int epollfd);

    //STATUS_RECEIVE_PASSWORD_LOAD
    bool process_STATUS_RECEIVE_PASSWORD_LOAD(UserInfo* user, int epollfd);

    //STATUS_LOGIN
    bool process_STATUS_LOGIN(UserInfo* user, int epollfd);

    //STATUS_LOGIN_OVER
    bool process_STATUS_LOGIN_OVER(UserInfo* user, int epollfd);

    //STATUS_REGISTER_OR_LOAD_OVER
    bool process_STATUS_REGISTER_OR_LOAD_OVER(UserInfo* user, int epollfd);
    
};

#endif