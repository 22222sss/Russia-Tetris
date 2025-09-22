#pragma once
#ifndef Server_H
#define Server_H

#include"../User/User.h"

class Server
{
public:
	Server();

    //�����û���Ϣ
    static int ReceiveData(User* user);

    //���س�ʼ�˵�����
    static int returnToInitMenu(User* user);

    //���ص�¼����
    static int returnToLoadMenu(User* user,int i);

    //����ע���û���
    static int receive_username_register(User* user);

    //����ע������
    static int receive_password_register(User* user);

    //���յ�¼�û���
    static int receive_username_load(User* user);

    //���յ�¼����
    static int receive_password_load(User* user);

    //�����û���¼�ɹ�����߼�
    static int loginUser(User* user);

    //STATUS_NOTSTART
    static bool process_STATUS_NOTSTART(User* user);

    //STATUS_RECEIVE_USERNAME_REGISTER
    static bool process_STATUS_RECEIVE_USERNAME_REGISTER(User* user);

    //STATUS_RECEIVE_PASSWORD_REGISTER
    static bool process_STATUS_RECEIVE_PASSWORD_REGISTER(User* user);

    //STATUS_RECEIVE_USERNAME_LOAD
    static bool process_STATUS_RECEIVE_USERNAME_LOAD(User* user);

    //STATUS_RECEIVE_PASSWORD_LOAD
    static bool process_STATUS_RECEIVE_PASSWORD_LOAD(User* user);

    //STATUS_LOGIN
    static bool process_STATUS_LOGIN(User* user);

    //STATUS_LOGIN_OVER
    static bool process_STATUS_LOGIN_OVER(User* user);

    //STATUS_REGISTER_OR_LOAD_OVER
    static bool process_STATUS_REGISTER_OR_LOAD_OVER(User* user);
    
};

#endif