#include"UserInfo.h"
#include"Public.h"

bool output(UserInfo* user, string s);

bool moveTo(UserInfo* user, int row, int col);

bool ChangeCurrentColor(UserInfo* user, int n);

bool outputText(UserInfo* user, int row, int col, int n, string s);

bool outputgrade(UserInfo* user, int row, int col, int n, string s, int grade);

//��������
bool clear(UserInfo* user);

//�����û���߷ֺ����20�η���
bool Update_TopScore_RecentScore(UserInfo* user);