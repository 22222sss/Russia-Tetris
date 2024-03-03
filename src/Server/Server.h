#pragma once
#ifndef Server_H
#define Server_H

#include"../User/User.h"

class Server
{
public:
	Server();

    //�����û���Ϣ
    int ReceiveData(User* user);

    //���س�ʼ�˵�����
    int returnToInitMenu(User* user);

    //���ص�¼����
    int returnToLoadMenu(User* user,int i);

    //����ע���û���
    int receive_username_register(User* user);

    //����ע������
    int receive_password_register(User* user);

    //���յ�¼�û���
    int receive_username_load(User* user);

    //���յ�¼����
    int receive_password_load(User* user);

    //�����û���¼�ɹ�����߼�
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