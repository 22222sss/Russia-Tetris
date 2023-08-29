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

void moveTo(int row, int col)//移动到第row行第col列
{
	printf("\x1b[%d;%dH", row, col);
}

void InitInterface()//初始化界面
{

	for (int i = 0; i < ROW; i++)
	{
		for (int j = 0; j < COL + 10; j++)
		{
			if (j == 0 || j == COL - 1 || j == COL + 9)
			{
				face.data[i][j] = 1; //标记该位置有方块
				moveTo(i + 1, 2 * j + 1);//方块占一个行位置，两个列位置
				printf("■");
			}
			else if (i == ROW - 1)
			{
				face.data[i][j] = 1; //标记该位置有方块
				moveTo(i + 1, 2 * j + 1);
				printf("■");
			}
			else
				face.data[i][j] = 0; //标记该位置无方块
		}

	}

	for (int i = COL; i < COL + 10; i++)
	{
		face.data[11][i] = 1; //标记该位置有方块
		moveTo(11 + 1, 2 * i + 1);
		printf("■");
	}

	moveTo(2, 2 * COL + 1 + 1);
	printf("\33[40m");
	printf("Next:");

	moveTo(14, 2 * COL + 2);
	printf("\33[40m");
	printf("Score:%d", grade);
}

//初始化方块信息
void InitBlockInfo()
{
	int i;
	//“T”形
	for (i = 0; i <= 2; i++)
	{
		block[0][0].space[1][i] = 1;
	}
	block[0][0].space[2][1] = 1;

	//“L”形
	for (i = 1; i <= 3; i++)
	{
		block[1][0].space[i][1] = 1;
	}
	block[1][0].space[3][2] = 1;

	//“J”形
	for (i = 1; i <= 3; i++)
	{
		block[2][0].space[i][2] = 1;
	}
	block[2][0].space[3][1] = 1;

	for (int i = 0; i <= 1; i++)
	{
		//“Z”形
		block[3][0].space[1][i] = 1;
		block[3][0].space[2][i + 1] = 1;
		//“S”形
		block[4][0].space[1][i + 1] = 1;
		block[4][0].space[2][i] = 1;
		//“O”形
		block[5][0].space[1][i + 1] = 1;
		block[5][0].space[2][i + 1] = 1;
	}

	//“I”形
	for (i = 0; i <= 3; i++)
	{
		block[6][0].space[i][1] = 1;
	}

	for (int shape = 0; shape < 7; shape++)//7种形状
	{
		for (int form = 0; form < 3; form++)//4种形态（已经有了一种，这里每个还需要增加3种）
		{
			int temp[4][4] = { 0 };
			//获取第form种状态
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					temp[i][j] = block[shape][form].space[i][j];
				}
			}
			//将第form种形态顺时针旋转，得到第form+1种形态
			for (i = 0; i < 4; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					block[shape][form + 1].space[i][j] = temp[3 - j][i];
				}
			}
		}
	}
}