#pragma once
#ifndef Utility_H
#define Utility_H

#include"../Server/Server.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../PlayerInfo/PlayerInfo.h"
#include"../TetrisGame/TetrisGame.h"
#include"../EventLoop/EventLoop.h"
#include"../User/User.h"
#include"../UImanage/UImanage.h"
#include"../Filedata_manage/Filedata.h"

//���÷�����/�����׽���
bool IsSetSocketBlocking(int socket, bool blocking);

//��ɫ����
int Color(int c);

//��ʼ��������Ϣ
void InitBlockInfo();

// ����û��Ƿ����
bool isUserExists(const std::string& username);

//��ȡĿǰʱ��
string currenttime();

bool output(User* user, string s);

bool moveTo(User* user, int row, int col);

bool ChangeCurrentColor(User* user, int n);

bool outputText(User* user, int row, int col, int n, string s);

bool outputgrade(User* user, int row, int col, int n, string s, int grade);



#endif
