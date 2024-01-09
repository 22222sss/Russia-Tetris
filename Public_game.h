#include"UserInfo.h"
#include"Public.h"

bool output(UserInfo* user, string s);

bool moveTo(UserInfo* user, int row, int col);

bool ChangeCurrentColor(UserInfo* user, int n);

bool outputText(UserInfo* user, int row, int col, int n, string s);

bool outputgrade(UserInfo* user, int row, int col, int n, string s, int grade);

//清屏函数
bool clear(UserInfo* user);

//更新用户最高分和最近20次分数
bool Update_TopScore_RecentScore(UserInfo* user);