#pragma once
#ifndef UImanage_H
#define UImanage_H


#include"../Common/Common.h"
#include"../TetrisGame/User.h"
#include"../TetrisGame/Game.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../TetrisGame/Filedata.h"
#include"../EventLoop/EventLoop.h"
#include"../TetrisGame/PlayerInfo.h"

class UImanage
{
public:

	//展示初始界面
	static bool showInitMenu(const shared_ptr<User>& user);

	// 获取用户名
	static bool show_Receive_Username(const shared_ptr<User>& user);

	//展示用户名或密码输入错误的界面
	static bool show_Error_Message(int i, const shared_ptr<User>& user);

	//展示用户名输入为空的界面
	static bool show_Username_Empty_Error(int i, const shared_ptr<User>& user);

	//展示注册时用户已被注册的界面
	static bool show_Username_Taken_Error(int i, const shared_ptr<User>& user);

	// 获取密码
	static bool show_Receive_Password(const shared_ptr<User>& user);

	//展示密码输入为空的界面
	static bool show_Password_Empty_Error(int i, const shared_ptr<User>& user);

	//展示用户登录成功的界面
	static bool show_Register_Success(const shared_ptr<User>& user);

	//展示用户登录失败的界面
	static bool show_Login_Failure(const shared_ptr<User>& user);

	//展示登录界面
	static bool showLoadMenu(const shared_ptr<User>& user);

	// 查看本人最近20次比赛的分数
	static bool showRecentScores(const shared_ptr<User>& user);

	// 查看全服top成绩
	static bool showTopScores(const shared_ptr<User>& user);

	// 查看全服简单模式top成绩
	static bool showTopScores_Easy(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user);
	
	// 查看全服普通模式top成绩
	static bool showTopScores_Normal(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user);

	// 查看全服困难模式top成绩
	static bool showTopScores_Diffcult(int row, int col, vector<shared_ptr<PlayerInfo>>& show, const shared_ptr<User>& user);

	//展示游戏难度
	static bool showGameDifficulty(const shared_ptr<User>& user);

	//展示游戏结束界面
	static bool showover(const shared_ptr<User>& user);

	//清屏函数
	static bool clear(const shared_ptr<User>& user);

};



#endif