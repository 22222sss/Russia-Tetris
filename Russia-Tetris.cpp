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

int kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	if (ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}
	return 0;
}

void moveTo(int row, int col)
{
	printf("\x1b[%d;%dH", row, col);
}


void InitInterface()
{

	for (int i = 0; i < ROW; i++)
	{
		for (int j = 0; j < COL + 10; j++)
		{
			if (j == 0 || j == COL - 1 || j == COL + 9)
			{
				face.data[i][j] = 1; //标记该位置有方块
				moveTo(i + 1, 2 * j + 1);
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

//颜色设置
void color(int c)
{
	switch (c)
	{
	case 0:
		c = 35; //“T”形方块设置为紫色
		break;
	case 1:
	case 2:
		c = 31; //“L”形和“J”形方块设置为红色
		break;
	case 3:
	case 4:
		c = 36; //“Z”形和“S”形方块设置为绿色
		break;
	case 5:
		c = 33; //“O”形方块设置为黄色
		break;
	case 6:
		c = 34; //“I”形方块设置为浅蓝色
		break;
	default:
		c = 37; //其他默认设置为白色
		break;
	}
	printf("\33[%dm", c); //颜色设置
}


//画出方块
void DrawBlock(int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			if (block[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(row + i, 2 * (col + j));//光标跳转到指定位置
				printf("■"); //输出方块
			}
		}
	}
}

//空格覆盖
//无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
//而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
//在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。
void DrawSpace(int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (block[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(row + i, 2 * (col + j));//光标跳转到指定位置
				printf("  ");//打印空格覆盖（两个空格）
			}
		}

	}
}

//其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
//所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
//合法性判断
int IsLegal(int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if ((block[shape][form].space[i][j] == 1) && (face.data[row + i][col + j] == 1))
				return 0;
		}
	}
	return 1;
}

//判断得分与结束

// 判断得分
//从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
//因为被下移的行并没有进行判断，可能还存在满行。

//判断结束
//直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
//游戏结束后，除了给出游戏结束提示语之外，如果玩家本局游戏分数大于历史最高记录，则需要更新最高分到文件当中。
//游戏结束后询问玩家是否再来一局。
int JudeFunc()
{
	int i, j;
	//判断是否得分
	for (i = ROW - 2; i > 4; i--)
	{
		int sum = 0;
		for (j = 1; j < COL - 1; j++)
		{
			sum += face.data[i][j];
		}
		if (sum == 0)
			break;
		if (sum == COL - 2)//该行全是方块，可得分
		{
			grade += 10;
			moveTo(14, 2 * COL + 2);
			color(7);
			printf("Score:%d", grade);
			for (j = 1; j <= COL - 1; j++)//清除得分行的方块信息
			{
				face.data[i][j] = 0;
				moveTo(i, 2 * j);
				printf("  ");
			}
			//把被清除行上面的行整体向下挪一格
			for (int m = i; m > 1; m--)
			{
				sum = 0;//记录上一行的方块个数
				for (int n = 1; n < COL - 1; n++)
				{
					sum += face.data[m - 1][n];//统计上一行的方块个数
					face.data[m][n] = face.data[m - 1][n];//将上一行方块的标识移到下一行
					face.color[m][n] = face.color[m - 1][n];//将上一行方块的颜色编号移到下一行
					if (face.data[m][n] == 1)
					{
						moveTo(m, 2 * n);
						color(face.color[m][n]);//颜色设置为还方块的颜色
						printf("■"); //打印方块

					}
					else
					{
						moveTo(m, 2 * n);
						printf("  ");
					}
				}
				if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
					return 1; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
			}
		}
	}
	//判断游戏是否结束
	for (int j = 1; j < COL - 1; j++)
	{
		if (face.data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
		{
			sleep(1); //留给玩家反应时间
			if (!system("clear")) //清空屏幕
				color(7); //颜色设置为白色

			moveTo(ROW / 2, 2 * (COL / 3));
			printf("GAME OVER");
			while (1)
			{
				char ch;
				moveTo(ROW / 2 + 3, 2 * (COL / 3));
				printf("再来一局?(y/n):");
				cin >> ch;
				if (ch == 'y' || ch == 'Y')
				{
					if (!system("clear"))
						main();
				}
				else if (ch == 'n' || ch == 'N')
				{
					moveTo(ROW / 2 + 5, 2 * (COL / 3));
					exit(0);
				}
				else
				{
					moveTo(ROW / 2 + 4, 2 * (COL / 3));
					printf("选择错误，请再次选择");
				}
			}
		}
	}
	return 0; //判断结束，无需再调用该函数进行判断
}

//游戏主体逻辑函数
void StartGame()
{
	int shape = rand() % 7, form = rand() % 4; //随机获取方块的形状和形态
	while (1)
	{
		int t = 0;
		int nextShape = rand() % 7, nextForm = rand() % 4; //随机获取下一个方块的形状和形态
		int row = 1, col = COL / 2 - 1; //方块初始下落位置
		color(nextShape); //颜色设置为下一个方块的颜色
		DrawBlock(nextShape, nextForm, 3, COL + 3); //将下一个方块显示在右上角
		while (1)
		{
			color(shape); //颜色设置为当前正在下落的方块
			DrawBlock(shape, form, row, col); //将该方块显示在初始下落位置
			if (t == 0)
			{
				t = 150000;//这里t越小，方块下落越快（可以根据此设置游戏难度）
			}

			while (--t)
			{
				if (kbhit() != 0)
					break;
			}

			if (t == 0)//键盘未被敲击
			{
				if (IsLegal(shape, form, row + 1, col) == 0)//方块再下落就不合法了（已经到达底部）
				{
					//将当前方块的信息录入face当中
					//face:记录界面的每个位置是否有方块，若有方块还需记录该位置方块的颜色。
					for (int i = 0; i < 4; i++)
					{
						for (int j = 0; j < 4; j++)
						{
							if (block[shape][form].space[i][j] == 1)
							{
								face.data[row + i][col + j] = 1; //将该位置标记为有方块
								face.color[row + i][col + j] = shape; //记录该方块的颜色数值
							}
						}
					}
					while (JudeFunc()); //判断此次方块下落是否得分以及游戏是否结束
					break; //跳出当前死循环，准备进行下一个方块的下落
				}
				else//未到底部
				{
					DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
					row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）
				}
			}
			else
			{
				char ch = getchar();
				switch (ch)
				{
				case DOWN: //方向键：下
					if (IsLegal(shape, form, row + 1, col) == 1) //判断方块向下移动一位后是否合法
					{
						//方块下落后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）
					}
					break;
				case LEFT: //方向键：左
					if (IsLegal(shape, form, row, col - 1) == 1) //判断方块向左移动一位后是否合法
					{
						//方块左移后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）
					}
					break;
				case RIGHT: //方向键：右
					if (IsLegal(shape, form, row, col + 1) == 1) //判断方块向右移动一位后是否合法
					{
						//方块右移后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）
					}
					break;
				case SPACE: //空格键
					if (IsLegal(shape, (form + 1) % 4, row + 1, col) == 1) //判断方块旋转后是否合法
					{
						//方块旋转后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						row++; //纵坐标自增（总不能原地旋转吧）
						form = (form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）
					}
					break;
				}
			}
		}
		shape = nextShape, form = nextForm; //获取下一个方块的信息
		DrawSpace(nextShape, nextForm, 3, COL + 3); //将右上角的方块信息用空格覆盖
	}
}



int main()
{
	if (!system("clear"))
	{
		grade = 0;
		InitInterface(); //初始化界面
		InitBlockInfo(); //初始化方块信息
		srand((unsigned int)time(NULL)); //设置随机数生成的起点
		StartGame(); //开始游戏
	}
	moveTo(30, 1);
	cout << endl;
	return 0;
}