#pragma once
#ifndef TetrisGame_H
#define TetrisGame_H

#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

class TetrisGame
{
public:

    //ѡ����Ϸ�Ѷ�
    static int select_game_difficulty(User* user);

    //��ʵ�ڷ����ƶ������У���ʱ�޿̶����жϷ�����һ�α仯���λ���Ƿ�Ϸ���ֻ�кϷ��Ż�����ñ仯�Ľ��С�
    //��ν�Ƿ�������ָ�÷�������˸ñ仯�������˱������з����λ�á�
    //�Ϸ����ж�

    static bool IsLegal(User* user, int shape, int form, int row, int col);

    //�жϵ÷������

    // �жϵ÷�
    //���������жϣ���ĳһ�з���ȫ�����򽫸��з���������գ����������Ϸ��ķ���ȫ�����ƣ����ƽ����󷵻�1����ʾ�����ٴε��øú��������ж�
    //��Ϊ�����Ƶ��в�û�н����жϣ����ܻ��������С�

    static int Is_Increase_Score(User* user);

    //����Ŀǰ����
    static bool UpdateCurrentScore(User* user);

    //�жϽ���
    //ֱ���ж���Ϸ���������һ�е����Ƿ��з�����ڣ������ڷ��飬����Ϸ������
    //��Ϸ������ѯ������Ƿ�����һ�֡�

    static bool IsOver(User* user);




    //STATUS_PLAYING

   static bool process_STATUS_PLAYING(User* user);

   static void handleRecvError_STATUS_PLAYING(User* user);

    //���������ƶ�
    static bool handleMoveDown(User* user);

    //���������ƶ�
    static bool handleMoveLeft(User* user);

    //���������ƶ�
    static bool handleMoveRight(User* user);
    
    //��������ת
    static bool handleRotation(User* user);

    static bool handleReceivedData_STATUS_PLAYING(char* buffer, User* user);


    //STATUS_OVER_CONFIRMING

    static void handleRecvError_STATUS_OVER_CONFIRMING(User* user);

    static void handleDisconnect_STATUS_OVER_CONFIRMING(User* user);

    static bool handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, User* user);

    static bool process_STATUS_OVER_CONFIRMING(User* user);

    
    //STATUS_SELECT_GAME_DIFFICULTY

    static bool process_STATUS_SELECT_GAME_DIFFICULTY(User* user);


};

#endif 