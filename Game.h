#pragma once
#ifndef Game_H
#define Game_H

#include"UserInfo.h"
#include"Server.h"

class Game
{
public:

    //选择游戏难度
    int select_game_difficulty(Server* server, UserInfo* user, int epollfd);

    bool InitInterface(UserInfo* user);

    bool InitGameFace(UserInfo* user);

    //画出方块
    bool DrawBlock(UserInfo* user,int shape, int form, int row, int col);//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列

    //空格覆盖
    //无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
    //而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
    //在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

    bool DrawSpace(UserInfo* user, int shape, int form, int row, int col);

    //其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
    //所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
    //合法性判断

    bool IsLegal(UserInfo* user, int shape, int form, int row, int col);

    //判断得分与结束

    // 判断得分
    //从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
    //因为被下移的行并没有进行判断，可能还存在满行。

    int Is_Increase_Score(UserInfo* user);

    //更新目前分数
    bool UpdateCurrentScore(UserInfo* user);

    //判断结束
    //直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
    //游戏结束后询问玩家是否再来一局。

    bool IsOver(UserInfo* user);

    //展示结束界面
    bool showover(UserInfo* user);

    //重置用户信息
    void resetUserInfo(UserInfo* user);






    // 定义处理方块下降逻辑的函数
    void processBlockDown(UserInfo* user);

    //处理定时触发逻辑
    void handleTimedUserLogic(UserInfo* user);

    //没有键盘输入时，方块自动下降
    void processTimerEvent(int epollfd);




    //STATUS_PLAYING

    bool process_STATUS_PLAYING(UserInfo* user);

    void handleRecvError_STATUS_PLAYING(UserInfo* user);

    //处理向下移动
    bool handleMoveDown(UserInfo* user);

    //处理向左移动
    bool handleMoveLeft(UserInfo* user);

    //处理向右移动
    bool handleMoveRight(UserInfo* user);
    
    //处理方块旋转
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