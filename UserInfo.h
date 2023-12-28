
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

    //��������
    bool DrawBlock(int shape, int form, int row, int col);//row��col��ָ���Ƿ�����Ϣ���е�һ�е�һ�еķ���Ĵ�ӡλ��Ϊ��row�е�col��

    //�ո񸲸�
    //��������Ϸ��������ƶ���������ʾ�����Ͻ���һ���������ʾ������Ҫ����λ�õı任.
    //���ڱ仯֮ǰ�϶���Ҫ�Ƚ�֮ǰ��ӡ�ķ����ÿո���и��ǣ�Ȼ���ٴ�ӡ�仯��ķ���
    //�ڸ��Ƿ���ʱ�ر���Ҫע����ǣ�Ҫ����һ��С������Ҫ�������ո�

    bool DrawSpace(int shape, int form, int row, int col);

    //��ʵ�ڷ����ƶ������У���ʱ�޿̶����жϷ�����һ�α仯���λ���Ƿ�Ϸ���ֻ�кϷ��Ż�����ñ仯�Ľ��С�
    //��ν�Ƿ�������ָ�÷�������˸ñ仯�������˱������з����λ�á�
    //�Ϸ����ж�

    bool IsLegal(int shape, int form, int row, int col);

    //�жϵ÷������

    // �жϵ÷�
    //���������жϣ���ĳһ�з���ȫ�����򽫸��з���������գ����������Ϸ��ķ���ȫ�����ƣ����ƽ����󷵻�1����ʾ�����ٴε��øú��������ж�
    //��Ϊ�����Ƶ��в�û�н����жϣ����ܻ��������С�

    int Is_Increase_Score();

    bool UpdateCurrentScore();

    bool clear();

    //�жϽ���
    //ֱ���ж���Ϸ���������һ�е����Ƿ��з�����ڣ������ڷ��飬����Ϸ������
    //��Ϸ������ѯ������Ƿ�����һ�֡�

    bool IsOver();
    
    //չʾ��������
    bool showover();

    bool showInitMenu();

    // ע�����û�
    bool registerUser(int epollfd);

    //���س�ʼ�˵�����
    int returnToInitMenu(int epollfd);

    //�����û���Ϣ
    int ReceiveData(int epollfd);

    // �û���¼
    bool loadUser(int epollfd);

    // д���û������ļ���������º���û�����
    void saveUserData();

    bool InitGameFace();

    //չʾ��¼����
    bool showLoadMenu();

    // �鿴�������20�α����ķ���
    bool showRecentScores(int epollfd);

    // �鿴ȫ��top�ɼ�
    bool showTopScores(int epollfd);

    //չʾ��Ϸ�Ѷ�
    bool showGameDifficulty(int epollfd);

    //���ص�¼����
    int returnToLoadMenu(int i,int epollfd);

    //�����û���߷�
    bool Update_TopScore_RecentScore();

    //�����û���Ϣ
    void resetUserInfo();

    //����ע���û���
    int receive_username_register(int epollfd);

    //����ע������
    int receive_password_register(int epollfd);
    
    //���յ�¼�û���
    int receive_username_load(int epollfd);

    //���յ�¼����
    int receive_password_load(int epollfd);

    //�����û���¼�ɹ�����߼�
    int loginUser(int epollfd);

    //ѡ����Ϸ�Ѷ�
    int select_game_difficulty(int epollfd);

    // ���崦���û��߼��ĺ���
    void processBlockDown();

    //����ʱ�����߼�
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



