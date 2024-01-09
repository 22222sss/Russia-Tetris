#pragma once
#ifndef Server_H
#define Server_H

#include"UserInfo.h"
//#include"Game.h"

class Server
{
public:
	Server();

    //չʾ��ʼ����
    bool showInitMenu(UserInfo* user);

    // ע�����û�
    bool registerUser(UserInfo* user,int epollfd);

    //�����û���Ϣ
    int ReceiveData(UserInfo* user, int epollfd);

    //���س�ʼ�˵�����
    int returnToInitMenu(UserInfo* user,int epollfd);

    // �û���¼
    bool loadUser(UserInfo* user,int epollfd);

    // д���û������ļ���������º���û�����
    void saveUserData(UserInfo* user);

    //չʾ��¼����
    bool showLoadMenu(UserInfo* user);

    // �鿴�������20�α����ķ���
    bool showRecentScores(UserInfo* user,int epollfd);

    // �鿴ȫ��top�ɼ�
    bool showTopScores(UserInfo* user,int epollfd);

    //չʾ��Ϸ�Ѷ�
    bool showGameDifficulty(UserInfo* user,int epollfd);

    //���ص�¼����
    int returnToLoadMenu(UserInfo* user,int i, int epollfd);

    //����ע���û���
    int receive_username_register(UserInfo* user,int epollfd);

    //����ע������
    int receive_password_register(UserInfo* user,int epollfd);

    //���յ�¼�û���
    int receive_username_load(UserInfo* user,int epollfd);

    //���յ�¼����
    int receive_password_load(UserInfo* user,int epollfd);

    //�����û���¼�ɹ�����߼�
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