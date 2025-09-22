#pragma once
#ifndef UImanage_H
#define UImanage_H

#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

class UImanage
{
public:
	UImanage();

	//展示初始界面
	static bool showInitMenu(User* user);

	// 获取用户名
	static bool show_Receive_Username(User* user);

	//展示用户名或密码输入错误的界面
	static bool show_Error_Message(int i,User* user);

	//展示用户名输入为空的界面
	static bool show_Username_Empty_Error(int i, User* user);

	//展示注册时用户已被注册的界面
	static bool show_Username_Taken_Error(int i, User* user);

	// 获取密码
	static bool show_Receive_Password(User* user);

	//展示密码输入为空的界面
	static bool show_Password_Empty_Error(int i, User* user);

	//展示用户登录成功的界面
	static bool show_Register_Success(User* user);

	//展示用户登录失败的界面
	static bool show_Login_Failure(User* user);

	//展示登录界面
	static bool showLoadMenu(User* user);

	// 查看本人最近20次比赛的分数
	static bool showRecentScores(User* user);

	// 查看全服top成绩
	static bool showTopScores(User* user);

	// 查看全服简单模式top成绩
	static bool showTopScores_Easy(int row, int col,vector<PlayerInfo*> show,User* user);
	
	// 查看全服普通模式top成绩
	static bool showTopScores_Normal(int row, int col,vector<PlayerInfo*> show,User* user);

	// 查看全服困难模式top成绩
	static bool showTopScores_Diffcult(int row, int col,vector<PlayerInfo*> show, User* user);

	//展示游戏难度
	static bool showGameDifficulty(User* user);

	static bool InitInterface(User* user);

	static bool InitGameFace(User* user);

	//画出方块
	static bool DrawBlock(User* user, int shape, int form, int row, int col);//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列

	//空格覆盖
	//无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
	//而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
	//在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

	static bool DrawSpace(User* user, int shape, int form, int row, int col);

	//展示游戏结束界面
	static bool showover(User* user);

	//清屏函数
	static bool clear(User* user);

private:

};



#endif