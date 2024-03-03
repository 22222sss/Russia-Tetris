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

    //选择游戏难度
    int select_game_difficulty(User* user);

    //其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
    //所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
    //合法性判断

    bool IsLegal(User* user, int shape, int form, int row, int col);

    //判断得分与结束

    // 判断得分
    //从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
    //因为被下移的行并没有进行判断，可能还存在满行。

    int Is_Increase_Score(User* user);

    //更新目前分数
    bool UpdateCurrentScore(User* user);

    //判断结束
    //直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
    //游戏结束后询问玩家是否再来一局。

    bool IsOver(User* user);




    //STATUS_PLAYING

    bool process_STATUS_PLAYING(User* user);

    void handleRecvError_STATUS_PLAYING(User* user);

    //处理向下移动
    bool handleMoveDown(User* user);

    //处理向左移动
    bool handleMoveLeft(User* user);

    //处理向右移动
    bool handleMoveRight(User* user);
    
    //处理方块旋转
    bool handleRotation(User* user);

    bool handleReceivedData_STATUS_PLAYING(char* buffer, User* user);


    //STATUS_OVER_CONFIRMING

    void handleRecvError_STATUS_OVER_CONFIRMING(User* user);

    void handleDisconnect_STATUS_OVER_CONFIRMING(User* user);

    bool handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, User* user);

    bool process_STATUS_OVER_CONFIRMING(User* user);

    
    //STATUS_SELECT_GAME_DIFFICULTY

    bool process_STATUS_SELECT_GAME_DIFFICULTY(User* user);


};

#endif 