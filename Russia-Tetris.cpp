#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include<iostream>
#include <termios.h>
#include <fcntl.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <signal.h>

#define BUFFSIZE 2048
#define DEFAULT_PORT 9999    // 指定端口为9999
#define MAXLINK 2048
char buf[BUFFSIZE] = { 0 }; // 用于收发数据
int sock, client, ret;    // 定义服务端套接字和客户端套接字
using namespace std;
#define ROW 24 //游戏区行数
#define COL 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

#define SPACE 32 //空格键
#define ESC "\033" //Esc键

//红绿黄蓝白
#define COLOR_PURPLE 35
#define COLOR_RED 31
#define COLOR_GREEN 36
#define COLOR_YELLO 33
#define COLOR_BLUE 34
#define COLOR_WHITE 37


struct Face
{
	int data[ROW][COL + 10];//用于标记指定位置是否有方块（1为有，0为无）
	int color[ROW][COL + 10];//用于记录指定位置的方块颜色编码
}face;

struct Block
{
	int space[4][4];
}block[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

int grade = 0; //全局变量
int total = 0;//行数

void SetSocketBlocking(int socket, bool blocking);//切换阻塞与非阻塞状态
void GameMain();
void clear();//清屏函数
void InitInterface();//初始化界面
void moveTo(int row, int col);//输出跳转函数
void InitBlockInfo();//初始化方块信息
void color(int num);//颜色设置
//画出方块
void DrawBlock(int shape, int form, int x, int y);
//空格覆盖
void DrawSpace(int shape, int form, int x, int y);
//合法性判断
bool IsLegal(int shape, int form, int x, int y);
//判断得分
bool JudeScore();
//判断结束
void IsOver();
//游戏主体逻辑函数
void StartGame();
//主函数
int main();
//统计得分
void CurrentScore();


void output(string s)
{
	//cout << s;
	send(client, s.c_str(), s.length(), 0);
}

void outputgrade(string s, int grade)
{
	string command = s + to_string(grade);
	//cout << command;
	output(command);
}

void ChangeCurrentColor(int n)
{
	string command = "\33[" + to_string(n) + "m";
	//cout << command;
	output(command);
}

void SetSocketBlocking(int socket, bool blocking) {
	// 获取套接字标志
	int flags = fcntl(socket, F_GETFL, 0);
	if (flags < 0) {
		std::cerr << "Failed to get socket flags" << std::endl;
		return;
	}

	// 根据 blocking 参数切换回阻塞或非阻塞模式
	if (blocking) {
		flags &= ~O_NONBLOCK;  // 清除非阻塞标志
	}
	else {
		flags |= O_NONBLOCK;  // 设置非阻塞标志
	}

	// 设置套接字的新标志
	if (fcntl(socket, F_SETFL, flags) < 0) {
		std::cerr << "Failed to set socket mode" << std::endl;
		return;
	}
}




void GameMain()
{
	// 将客户端套接字设置为非阻塞模式
	SetSocketBlocking(client, false);

	grade = 0;
	InitInterface(); //初始化界面
	InitBlockInfo(); //初始化方块信息
	srand((unsigned int)time(NULL)); //设置随机数生成的起点
	StartGame(); //开始游戏

	moveTo(30, 1);
	cout << endl;
}


void clear()
{
	int i;
	string emptyLine(4 * COL, ' ');
	for (i = 1; i <= ROW; i++)
	{
		moveTo(i, 1);
		//cout << emptyLine;
		output(emptyLine);
	}
}

void moveTo(int row, int col)
{
	string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
	//cout << command; // 在控制台上打印命令
	output(command);
}


void CurrentScore()
{
	if (total >= 2)
	{
		grade += (total + 1) * 10;
		moveTo(14, 2 * COL + 2);
		color(7);
		//printf("Score:%d", grade);
		outputgrade("Score: ", grade);
	}
	else
	{
		grade += total * 10;
		moveTo(14, 2 * COL + 2);
		color(7);
		outputgrade("Score: ", grade);
	}
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
				moveTo(i + 1, 2 * (j + 1) - 1);
				output("■");
			}
			else if (i == ROW - 1)
			{
				face.data[i][j] = 1; //标记该位置有方块
				moveTo(i + 1, 2 * (j + 1) - 1);
				output("■");
			}
			else
				face.data[i][j] = 0; //标记该位置无方块
		}

	}

	for (int i = COL; i < COL + 10; i++)
	{
		face.data[11][i] = 1; //标记该位置有方块
		moveTo(11 + 1, 2 * i + 1);
		output("■");
	}

	moveTo(2, 2 * COL + 1 + 1);
	ChangeCurrentColor(COLOR_WHITE);
	output("Next:");

	moveTo(14, 2 * COL + 2);
	ChangeCurrentColor(COLOR_WHITE);
	outputgrade("Score: ", grade);
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
		ChangeCurrentColor(COLOR_PURPLE); //“T”形方块设置为紫色
		break;
	case 1:
	case 2:
		ChangeCurrentColor(COLOR_RED); //“L”形和“J”形方块设置为红色
		break;
	case 3:
	case 4:
		ChangeCurrentColor(COLOR_GREEN);//“Z”形和“S”形方块设置为绿色
		break;
	case 5:
		ChangeCurrentColor(COLOR_YELLO);//“O”形方块设置为黄色
		break;
	case 6:
		ChangeCurrentColor(COLOR_BLUE);//“I”形方块设置为浅蓝色
		break;
	default:
		ChangeCurrentColor(COLOR_WHITE); //其他默认设置为白色
		break;
	}
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
				moveTo(row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("■"); //输出方块
				output("■");
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
				moveTo(row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("  ");//打印空格覆盖（两个空格）
				output("  ");
			}
		}

	}
}


//其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
//所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
//合法性判断
bool IsLegal(int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if ((block[shape][form].space[i][j] == 1) && (face.data[row + i - 1][col + j - 1] == 1))
				return false;
		}
	}
	return true;
}

//判断得分与结束

// 判断得分
//从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
//因为被下移的行并没有进行判断，可能还存在满行。

//判断结束
//直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
//游戏结束后询问玩家是否再来一局。
bool JudeScore()
{
	int i = 0, j = 0;
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
			total++;
			for (j = 1; j < COL - 1; j++)//清除得分行的方块信息
			{
				face.data[i][j] = 0;
				moveTo(i + 1, 2 * j + 1);
				//printf("  ");
				output("  ");

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
						moveTo(m + 1, 2 * n + 1);
						color(face.color[m][n]);//颜色设置为还方块的颜色
						//printf("■"); //打印方块
						output("■");
					}
					else
					{
						moveTo(m + 1, 2 * n + 1);
						//printf("  ");
						output("  ");
					}
				}
				if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
					return true; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
			}
		}
	}
	return false;
}

void IsOver()
{
	//判断游戏是否结束
	for (int j = 1; j < COL - 1; j++)
	{
		if (face.data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
		{
			sleep(1); //留给玩家反应时间
			color(7); //颜色设置为白色
			moveTo(ROW / 2, 2 * (COL / 3));
			output("GAME OVER");
			while (1)
			{
				moveTo(ROW / 2 + 3, 2 * (COL / 3));
				output("Start Again ? (y/n):");

				// 将客户端套接字设置为阻塞模式
				SetSocketBlocking(client, true);

				recv(client, buf, sizeof(buf), 0);

				if (*buf == 'Y' || *buf == 'y')
				{
					clear();
					GameMain();
				}
				else if (*buf == 'n' || *buf == 'N')
				{
					moveTo(ROW / 2 + 5, 2 * (COL / 3));
					close(client);
					close(sock);
				}
				else
				{
					moveTo(ROW / 2 + 4, 2 * (COL / 3));
					//printf("选择错误，请再次选择");
					output("选择错误，请再次选择");
				}
			}
		}
	}

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
				t = 10000000;//这里t越小，方块下落越快（可以根据此设置游戏难度）
			}

			while (--t)
			{
				int bytesRead = recv(client, buf, sizeof(buf), 0);
				if (bytesRead > 0)
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
								face.data[row + i - 1][col + j - 1] = 1; //将该位置标记为有方块
								face.color[row + i - 1][col + j - 1] = shape; //记录该方块的颜色数值
							}
						}
					}
					total = 0;
					while (JudeScore());//判断此次方块下落是否得分
					CurrentScore();//统计得分
					IsOver();
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

				if (strcmp(buf, KEY_DOWN) == 0)//下
				{
					if (IsLegal(shape, form, row + 1, col) == 1) //判断方块向下移动一位后是否合法
					{
						//方块下落后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）
					}
				}
				else if (strcmp(buf, KEY_LEFT) == 0)//左
				{
					if (IsLegal(shape, form, row, col - 1) == 1) //判断方块向左移动一位后是否合法
					{
						//方块左移后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）
					}
				}
				else if (strcmp(buf, KEY_RIGHT) == 0)//右
				{
					if (IsLegal(shape, form, row, col + 1) == 1) //判断方块向右移动一位后是否合法
					{
						//方块右移后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）
					}
				}
				else
				{
					if (IsLegal(shape, (form + 1) % 4, row + 1, col) == 1) //判断方块旋转后是否合法
					{
						//方块旋转后合法才进行以下操作
						DrawSpace(shape, form, row, col); //用空格覆盖当前方块所在位置
						row++; //纵坐标自增（总不能原地旋转吧）
						form = (form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）
					}
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
		struct sockaddr_in addr;    // 用于存放ip和端口的结构
		// 对应伪代码中的sockfd = socket();
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (-1 == sock)
		{
			printf("创建套接字失败\n");
			return -1;
		}
		// END
		// 对应伪代码中的bind(sockfd, ip::port和一些配置);
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(DEFAULT_PORT);
		if (-1 == bind(sock, (struct sockaddr*)&addr, sizeof(addr)))
		{
			printf("绑定地址端口失败\n");
			return -1;
		}
		// END
		// 对应伪代码中的listen(sockfd);    
		if (-1 == listen(sock, MAXLINK))
		{
			printf("监听套接字失败\n");
			return -1;
		}
		// END
		//printf("listen.......\n");
		//printf("start to accepct\n");
		//printf("client=%d\n", client);
		client = accept(sock, NULL, NULL);
		GameMain();
		return 0;
	}
}