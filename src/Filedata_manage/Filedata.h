#pragma once
#ifndef Filedata_H
#define Filedata_H

#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

class Filedata
{
public:
	Filedata();

	vector<string> Read_recent_grades(User *user);

	vector<PlayerInfo*> Read_AllpalyerInfo(ifstream &file);

	// 写入用户数据文件，保存更新后的用户数据
	bool saveNewUserData(User* user);

	// 读取用户数据文件，初始化players向量
	bool loadPlayerData();

	//更新用户最高分和最近20次分数
	bool Update_TopScore_RecentScore(User* user);

private:

};

#endif