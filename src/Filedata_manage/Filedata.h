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

	// д���û������ļ���������º���û�����
	bool saveNewUserData(User* user);

	// ��ȡ�û������ļ�����ʼ��players����
	bool loadPlayerData();

	//�����û���߷ֺ����20�η���
	bool Update_TopScore_RecentScore(User* user);

private:

};

#endif