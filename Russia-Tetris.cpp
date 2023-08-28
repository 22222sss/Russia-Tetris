#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include<iostream>
#include <termios.h>
#include <fcntl.h>
using namespace std;
#define ROW 24 //游戏区行数
#define COL 20 //游戏区列数

#define DOWN  's'//方向键：下
#define LEFT  'a'//方向键：左
#define RIGHT 'd'//方向键：右
#define SPACE 32 //空格键
#define ESC "\033" //Esc键

struct Face
{
	int data[ROW][COL + 10];//用于标记指定位置是否有方块（1为有，0为无）
	int color[ROW][COL + 10];//用于记录指定位置的方块颜色编码
}face;

struct Block
{
	int space[4][4];
}block[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

int grade; //全局变量

void InitInterface();//初始化界面
void moveTo(int row, int col);//输出跳转函数
void InitBlockInfo();//初始化方块信息
void color(int num);//颜色设置
//画出方块
void DrawBlock(int shape, int form, int x, int y);
//空格覆盖
void DrawSpace(int shape, int form, int x, int y);
//合法性判断
int IsLegal(int shape, int form, int x, int y);
//判断得分与结束
int JudeFunc();
//游戏主体逻辑函数
void StartGame();
//主函数
int main();