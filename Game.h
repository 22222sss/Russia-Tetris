#pragma once
#ifndef Game_H
#define Game_H

#include"UserInfo.h"
#include"Server.h"

class Game
{
public:

    //ѡ����Ϸ�Ѷ�
    int select_game_difficulty(Server* server, UserInfo* user, int epollfd);

    bool InitInterface(UserInfo* user);

    bool InitGameFace(UserInfo* user);

    //��������
    bool DrawBlock(UserInfo* user,int shape, int form, int row, int col);//row��col��ָ���Ƿ�����Ϣ���е�һ�е�һ�еķ���Ĵ�ӡλ��Ϊ��row�е�col��

    //�ո񸲸�
    //��������Ϸ��������ƶ���������ʾ�����Ͻ���һ���������ʾ������Ҫ����λ�õı任.
    //���ڱ仯֮ǰ�϶���Ҫ�Ƚ�֮ǰ��ӡ�ķ����ÿո���и��ǣ�Ȼ���ٴ�ӡ�仯��ķ���
    //�ڸ��Ƿ���ʱ�ر���Ҫע����ǣ�Ҫ����һ��С������Ҫ�������ո�

    bool DrawSpace(UserInfo* user, int shape, int form, int row, int col);

    //��ʵ�ڷ����ƶ������У���ʱ�޿̶����жϷ�����һ�α仯���λ���Ƿ�Ϸ���ֻ�кϷ��Ż�����ñ仯�Ľ��С�
    //��ν�Ƿ�������ָ�÷�������˸ñ仯�������˱������з����λ�á�
    //�Ϸ����ж�

    bool IsLegal(UserInfo* user, int shape, int form, int row, int col);

    //�жϵ÷������

    // �жϵ÷�
    //���������жϣ���ĳһ�з���ȫ�����򽫸��з���������գ����������Ϸ��ķ���ȫ�����ƣ����ƽ����󷵻�1����ʾ�����ٴε��øú��������ж�
    //��Ϊ�����Ƶ��в�û�н����жϣ����ܻ��������С�

    int Is_Increase_Score(UserInfo* user);

    //����Ŀǰ����
    bool UpdateCurrentScore(UserInfo* user);

    //�жϽ���
    //ֱ���ж���Ϸ���������һ�е����Ƿ��з�����ڣ������ڷ��飬����Ϸ������
    //��Ϸ������ѯ������Ƿ�����һ�֡�

    bool IsOver(UserInfo* user);

    //չʾ��������
    bool showover(UserInfo* user);

    //�����û���Ϣ
    void resetUserInfo(UserInfo* user);






    // ���崦�����½��߼��ĺ���
    void processBlockDown(UserInfo* user);

    //����ʱ�����߼�
    void handleTimedUserLogic(UserInfo* user);

    //û�м�������ʱ�������Զ��½�
    void processTimerEvent(int epollfd);




    //STATUS_PLAYING

    bool process_STATUS_PLAYING(UserInfo* user);

    void handleRecvError_STATUS_PLAYING(UserInfo* user);

    //���������ƶ�
    bool handleMoveDown(UserInfo* user);

    //���������ƶ�
    bool handleMoveLeft(UserInfo* user);

    //���������ƶ�
    bool handleMoveRight(UserInfo* user);
    
    //��������ת
    bool handleRotation(UserInfo* user);

    bool handleReceivedData_STATUS_PLAYING(char* buffer, UserInfo* user);


    //STATUS_OVER_CONFIRMING

    void handleRecvError_STATUS_OVER_CONFIRMING(UserInfo* user);

    void handleDisconnect_STATUS_OVER_CONFIRMING(UserInfo* user);

    bool handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, Server* server, UserInfo* user, int epollfd);

    bool process_STATUS_OVER_CONFIRMING(Server* server, UserInfo* user, int epollfd);

    
    //STATUS_SELECT_GAME_DIFFICULTY

    bool process_STATUS_SELECT_GAME_DIFFICULTY(Server* server, UserInfo* user, int epollfd);







};

#endif 