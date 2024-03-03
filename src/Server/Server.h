#pragma once
#ifndef Server_H
#define Server_H

#include"../User/User.h"

class Server
{
public:
	Server();

    //接收用户信息
    int ReceiveData(User* user);

    //返回初始菜单界面
    int returnToInitMenu(User* user);

    //返回登录界面
    int returnToLoadMenu(User* user,int i);

    //接收注册用户名
    int receive_username_register(User* user);

    //接收注册密码
    int receive_password_register(User* user);

    //接收登录用户名
    int receive_username_load(User* user);

    //接收登录密码
    int receive_password_load(User* user);

    //处理用户登录成功后的逻辑
    int loginUser(User* user);

    //STATUS_NOTSTART
    bool process_STATUS_NOTSTART(User* user);

    //STATUS_RECEIVE_USERNAME_REGISTER
    bool process_STATUS_RECEIVE_USERNAME_REGISTER(User* user);

    //STATUS_RECEIVE_PASSWORD_REGISTER
    bool process_STATUS_RECEIVE_PASSWORD_REGISTER(User* user);

    //STATUS_RECEIVE_USERNAME_LOAD
    bool process_STATUS_RECEIVE_USERNAME_LOAD(User* user);

    //STATUS_RECEIVE_PASSWORD_LOAD
    bool process_STATUS_RECEIVE_PASSWORD_LOAD(User* user);

    //STATUS_LOGIN
    bool process_STATUS_LOGIN(User* user);

    //STATUS_LOGIN_OVER
    bool process_STATUS_LOGIN_OVER(User* user);

    //STATUS_REGISTER_OR_LOAD_OVER
    bool process_STATUS_REGISTER_OR_LOAD_OVER(User* user);
    
};

#endif