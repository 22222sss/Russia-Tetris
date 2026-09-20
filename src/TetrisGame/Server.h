#pragma once
#ifndef Server_H
#define Server_H

#include"User.h"

class Server
{
public:

    //接收用户信息
    static int ReceiveData(shared_ptr<User>& user);

    //返回初始菜单界面
    static int returnToInitMenu(shared_ptr<User>& user);

    //返回登录界面
    static int returnToLoadMenu(shared_ptr<User>& user,int i);

    //接收注册用户名
    static int receive_username_register(shared_ptr<User>& user);

    //接收注册密码
    static int receive_password_register(shared_ptr<User>& user);

    //接收登录用户名
    static int receive_username_load(shared_ptr<User>& user);

    //接收登录密码
    static int receive_password_load(shared_ptr<User>& user);

    //处理用户登录成功后的逻辑
    static int loginUser(shared_ptr<User>& user);

    //STATUS_NOTSTART
    static bool process_STATUS_NOTSTART(shared_ptr<User>& user);

    //STATUS_RECEIVE_USERNAME_REGISTER
    static bool process_STATUS_RECEIVE_USERNAME_REGISTER(shared_ptr<User>& user);

    //STATUS_RECEIVE_PASSWORD_REGISTER
    static bool process_STATUS_RECEIVE_PASSWORD_REGISTER(shared_ptr<User>& user);

    //STATUS_RECEIVE_USERNAME_LOAD
    static bool process_STATUS_RECEIVE_USERNAME_LOAD(shared_ptr<User>& user);

    //STATUS_RECEIVE_PASSWORD_LOAD
    static bool process_STATUS_RECEIVE_PASSWORD_LOAD(shared_ptr<User>& user);

    //STATUS_LOGIN
    static bool process_STATUS_LOGIN(shared_ptr<User>& user);

    //STATUS_LOGIN_OVER
    static bool process_STATUS_LOGIN_OVER(shared_ptr<User>& user);

    //STATUS_REGISTER_OR_LOAD_OVER
    static bool process_STATUS_REGISTER_OR_LOAD_OVER(shared_ptr<User>& user);
    
    //处理新用户的连接
    static void handleNewClientConnection(int serverSocket, short events, void* arg);

    //处理已连接用户的键盘输入
    static void handleClientData(int clientSocket, short events, void* arg);

};

#endif