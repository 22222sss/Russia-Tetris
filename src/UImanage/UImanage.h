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

	//展示注册密码太短的界面（密码策略见 TetrisGame/PasswordPolicy.h）
	static bool show_Password_Too_Short_Error(int i, const shared_ptr<User>& user);

	//展示用户登录成功的界面
	static bool show_Register_Success(const shared_ptr<User>& user);

	//展示用户登录失败的界面
	static bool show_Login_Failure(const shared_ptr<User>& user);

	//展示"登录尝试过于频繁，请 N 秒后再试"（失败退避，见 TetrisGame/AuthThrottle.h）
	static bool show_Login_Throttled(int retryAfterSeconds, const shared_ptr<User>& user);

	//展示"注册过于频繁，请 N 秒后再试"（按 IP + 全局的注册限流，见 TetrisGame/RegThrottle.h）
	static bool show_Registration_Throttled(int retryAfterSeconds, const shared_ptr<User>& user);

	//展示"服务繁忙"（哈希队列已满，本次没有校验）
	static bool show_Server_Busy(const shared_ptr<User>& user);

	//展示"修改密码：请输入当前密码"（见 TetrisGame/Filedata.h 的 changePasswordAsync）
	static bool show_Change_Password_Old(const shared_ptr<User>& user);

	//展示"请输入新密码（至少 8 个字符）"
	static bool show_Receive_New_Password(const shared_ptr<User>& user);

	//新密码与当前密码相同：留在"输入新密码"状态
	static bool show_Password_Unchanged_Error(const shared_ptr<User>& user);

	//密码修改成功（按 3 回主菜单）
	static bool show_Password_Changed(const shared_ptr<User>& user);

	//修改密码失败（当前密码错 / 服务繁忙 / 存储错误 / 被退避拒绝），按 3 回主菜单
	static bool show_Change_Password_Error(const string& message, const shared_ptr<User>& user);

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

	// 隐藏/显示光标（游戏进行中藏起来，回到菜单再显示）
	static bool hideCursor(const shared_ptr<User>& user);

};



#endif