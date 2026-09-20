#pragma once
#ifndef Game_H
#define Game_H

#include"User.h"
#include"Game.h"
#include"Common.h"
#include"Server.h"
#include"Filedata.h"
#include"PlayerInfo.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../EventLoop/EventLoop.h"

struct Block
{
    int space[4][4];
};

//enum（枚举）:枚举是一种用户定义的数据类型，用于给整数值赋予有意义的名称，提高代码可读性。

enum Shape
{
    SHAPE_T = 0,
    SHAPE_L = 1,
    SHAPE_J = 2,
    SHAPE_Z = 3,
    SHAPE_S = 4,
    SHAPE_O = 5,
    SHAPE_I = 6
};

enum Color
{
    COLOR_PURPLE = 35,
    COLOR_RED = 31,
    COLOR_LOWBLUE = 36,
    COLOR_YELLO = 33,
    COLOR_DEEPBLUE = 34,
    COLOR_WHITE = 37
};



class Game
{
private:

    inline static Block blockDefines[7][4] = {0};//用于存储7种基本形状方块的各自的4种形态的信息，共28种
    //inline 静态成员，可以在类内直接初始化
public:

    //初始化方块信息
    static void InitBlockInfo();

    static bool InitInterface(std::shared_ptr<User>& user);

    static bool InitGameFace(std::shared_ptr<User>& user);

    //画出方块
    static bool DrawBlock(std::shared_ptr<User>& user, int shape, int form, int row, int col);//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列

    //空格覆盖
    //无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
    //而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
    //在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

    static bool DrawSpace(std::shared_ptr<User>& user, int shape, int form, int row, int col);


    //选择游戏难度
    static int select_game_difficulty(std::shared_ptr<User>& user);

    //其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
    //所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
    //合法性判断

    static bool IsLegal(std::shared_ptr<User>& user, int shape, int form, int row, int col);

    //判断得分与结束

    // 判断得分
    //从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
    //因为被下移的行并没有进行判断，可能还存在满行。

    static int Is_Increase_Score(std::shared_ptr<User>& user);

    //更新目前分数
    static bool UpdateCurrentScore(std::shared_ptr<User>& user);

    //判断结束
    //直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
    //游戏结束后询问玩家是否再来一局。

    static bool IsOver(std::shared_ptr<User>& user);




    //STATUS_PLAYING

   static bool process_STATUS_PLAYING(std::shared_ptr<User>& user);

   static void handleRecvError_STATUS_PLAYING(std::shared_ptr<User>& user);

    //处理向下移动
    static bool handleMoveDown(std::shared_ptr<User>& user);

    //处理向左移动
    static bool handleMoveLeft(std::shared_ptr<User>& user);

    //处理向右移动
    static bool handleMoveRight(std::shared_ptr<User>& user);
    
    //处理方块旋转
    static bool handleRotation(std::shared_ptr<User>& user);

    static bool handleReceivedData_STATUS_PLAYING(char* buffer, std::shared_ptr<User>& user);


    //STATUS_OVER_CONFIRMING

    static void handleRecvError_STATUS_OVER_CONFIRMING(std::shared_ptr<User>& user);

    static void handleDisconnect_STATUS_OVER_CONFIRMING(std::shared_ptr<User>& user);

    static bool handleBufferData_STATUS_OVER_CONFIRMING(char* buffer, std::shared_ptr<User>& user);

    static bool process_STATUS_OVER_CONFIRMING(std::shared_ptr<User>& user);

    
    //STATUS_SELECT_GAME_DIFFICULTY

    static bool process_STATUS_SELECT_GAME_DIFFICULTY(std::shared_ptr<User>& user);

    //时间事件

    // 定义处理方块下降逻辑的函数
    static void processBlockDown(std::shared_ptr<User>& user);

    //处理定时触发逻辑
    static void handleTimedUserLogic(std::shared_ptr<User>& user);

    //没有键盘输入时，方块自动下降
    static void processTimerEvent(int timerfd, short events, void* arg);
};

#endif 