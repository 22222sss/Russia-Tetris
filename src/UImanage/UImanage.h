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

	//չʾ��ʼ����
	bool showInitMenu(User* user);

	// ��ȡ�û���
	bool show_Receive_Username(User* user);

	//չʾ�û����������������Ľ���
	bool show_Error_Message(int i,User* user);

	//չʾ�û�������Ϊ�յĽ���
	bool show_Username_Empty_Error(int i, User* user);

	//չʾע��ʱ�û��ѱ�ע��Ľ���
	bool show_Username_Taken_Error(int i, User* user);

	// ��ȡ����
	bool show_Receive_Password(User* user);

	//չʾ��������Ϊ�յĽ���
	bool show_Password_Empty_Error(int i, User* user);

	//չʾ�û���¼�ɹ��Ľ���
	bool show_Register_Success(User* user);

	//չʾ�û���¼ʧ�ܵĽ���
	bool show_Login_Failure(User* user);

	//չʾ��¼����
	bool showLoadMenu(User* user);

	// �鿴�������20�α����ķ���
	bool showRecentScores(User* user);

	// �鿴ȫ��top�ɼ�
	bool showTopScores(User* user);

	// �鿴ȫ����ģʽtop�ɼ�
	bool showTopScores_Easy(int row, int col,vector<PlayerInfo*> show,User* user);
	
	// �鿴ȫ����ͨģʽtop�ɼ�
	bool showTopScores_Normal(int row, int col,vector<PlayerInfo*> show,User* user);

	// �鿴ȫ������ģʽtop�ɼ�
	bool showTopScores_Diffcult(int row, int col,vector<PlayerInfo*> show, User* user);

	//չʾ��Ϸ�Ѷ�
	bool showGameDifficulty(User* user);

	bool InitInterface(User* user);

	bool InitGameFace(User* user);

	//��������
	bool DrawBlock(User* user, int shape, int form, int row, int col);//row��col��ָ���Ƿ�����Ϣ���е�һ�е�һ�еķ���Ĵ�ӡλ��Ϊ��row�е�col��

	//�ո񸲸�
	//��������Ϸ��������ƶ���������ʾ�����Ͻ���һ���������ʾ������Ҫ����λ�õı任.
	//���ڱ仯֮ǰ�϶���Ҫ�Ƚ�֮ǰ��ӡ�ķ����ÿո���и��ǣ�Ȼ���ٴ�ӡ�仯��ķ���
	//�ڸ��Ƿ���ʱ�ر���Ҫע����ǣ�Ҫ����һ��С������Ҫ�������ո�

	bool DrawSpace(User* user, int shape, int form, int row, int col);

	//չʾ��Ϸ��������
	bool showover(User* user);

	//��������
	bool clear(User* user);

private:

};



#endif