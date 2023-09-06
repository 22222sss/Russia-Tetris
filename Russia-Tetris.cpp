#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include<iostream>
#include<string>
#include <termios.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <sys/epoll.h>	//epoll头文件
#include <signal.h>
#include <unordered_map>
#include<vector>
#include <thread>
#define MAXSIZE 2048
#define DEFAULT_PORT 8880// 指定端口为9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define ROW 24 //游戏区行数
#define COL 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

//红绿黄蓝白
#define COLOR_PURPLE 35
#define COLOR_RED 31
#define COLOR_GREEN 36
#define COLOR_YELLO 33
#define COLOR_BLUE 34
#define COLOR_WHITE 37

using namespace std;

typedef struct UserInfo
{
	int fd;
	int line;
	int score;
	int shape;
	int form;
	int nextShape;
	int nextForm;
	int row;
	int col;

	int data[ROW][COL + 10];//用于标记指定位置是否有方块（1为有，0为无）
	int color[ROW][COL + 10];//用于记录指定位置的方块颜色编码

}UserInfo;

unordered_map<int, UserInfo*> p;

vector<int> qiut;

struct Block
{
	int space[4][4];
}block[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

/*
void output(int client, string s);

void outputgrade(int client, string s, int grade);

void moveTo(int client, int row, int col);

void ChangeCurrentColor(int client, int n);

void SetSocketBlocking(int socket, bool blocking);

void InitBlockInfo();

void DrawBlock(int client, int shape, int form, int row, int col);

bool IsLegal(UserInfo* user, int shape, int form, int row, int col);

bool JudeScore(UserInfo* userInfo);

void CurrentScore(UserInfo* userInfo);

bool IsOver(int serverSocket, UserInfo* userInfo);

void clear(int client);

void HandleClientConnection(int serverSocket, vector<UserInfo*>& p, int epollfd);

void processTimerEvent(int timerfd, vector<UserInfo*>& p, int epollfd);

void handleClientData(int epollfd, UserInfo* userInfo);

void processEvents(int readyCount, epoll_event* events, int serverSocket, int timerfd, vector<UserInfo*>& p, int epollfd);

void processUserLogic(UserInfo* user, int epollfd);
*/
void InitInterface(UserInfo* user);






void output(int client, string s)
{
	send(client, s.c_str(), s.length(), 0);
}

void outputgrade(int client, string s, int grade)
{
	string command = s + to_string(grade);
	output(client, command);
}

void moveTo(int client, int row, int col)
{
	string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
	//cout << command; // 在控制台上打印命令
	output(client, command);
}

void ChangeCurrentColor(int client, int n)
{
	string command = "\33[" + to_string(n) + "m";
	//cout << command;
	output(client, command);
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

//画出方块
void DrawBlock(int client, int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			if (block[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(client, row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("■"); //输出方块
				output(client, "■");
			}
		}
	}
}

//空格覆盖
//无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
//而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
//在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

void DrawSpace(int client, int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (block[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(client, row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("  ");//打印空格覆盖（两个空格）
				output(client, "  ");
			}
		}

	}
}

//颜色设置
void color(int client, int c)
{
	switch (c)
	{
	case 0:
		ChangeCurrentColor(client, COLOR_PURPLE); //“T”形方块设置为紫色
		break;
	case 1:
	case 2:
		ChangeCurrentColor(client, COLOR_RED); //“L”形和“J”形方块设置为红色
		break;
	case 3:
	case 4:
		ChangeCurrentColor(client, COLOR_GREEN);//“Z”形和“S”形方块设置为绿色
		break;
	case 5:
		ChangeCurrentColor(client, COLOR_YELLO);//“O”形方块设置为黄色
		break;
	case 6:
		ChangeCurrentColor(client, COLOR_BLUE);//“I”形方块设置为浅蓝色
		break;
	default:
		ChangeCurrentColor(client, COLOR_WHITE); //其他默认设置为白色
		break;
	}
}

//其实在方块移动过程中，无时无刻都在判断方块下一次变化后的位置是否合法，只有合法才会允许该变化的进行。
//所谓非法，就是指该方块进行了该变化后落在了本来就有方块的位置。
//合法性判断
bool IsLegal(UserInfo* user, int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if ((block[shape][form].space[i][j] == 1) && (user->data[row + i - 1][col + j - 1] == 1))
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
bool JudeScore(UserInfo* userInfo)
{
	int i = 0, j = 0;
	//判断是否得分
	for (i = ROW - 2; i > 4; i--)
	{
		int sum = 0;
		for (j = 1; j < COL - 1; j++)
		{
			sum += userInfo->data[i][j];
		}
		if (sum == 0)
			break;
		if (sum == COL - 2)//该行全是方块，可得分
		{
			userInfo->line++;
			for (j = 1; j < COL - 1; j++)//清除得分行的方块信息
			{
				userInfo->data[i][j] = 0;
				moveTo(userInfo->fd, i + 1, 2 * j + 1);
				//printf("  ");
				output(userInfo->fd, "  ");

			}
			//把被清除行上面的行整体向下挪一格
			for (int m = i; m > 1; m--)
			{
				sum = 0;//记录上一行的方块个数
				for (int n = 1; n < COL - 1; n++)
				{
					sum += userInfo->data[m - 1][n];//统计上一行的方块个数
					userInfo->data[m][n] = userInfo->data[m - 1][n];//将上一行方块的标识移到下一行
					userInfo->color[m][n] = userInfo->color[m - 1][n];//将上一行方块的颜色编号移到下一行
					if (userInfo->data[m][n] == 1)
					{
						moveTo(userInfo->fd, m + 1, 2 * n + 1);
						color(userInfo->fd, userInfo->color[m][n]);//颜色设置为还方块的颜色
						//printf("■"); //打印方块
						output(userInfo->fd, "■");
					}
					else
					{
						moveTo(userInfo->fd, m + 1, 2 * n + 1);
						//printf("  ");
						output(userInfo->fd, "  ");
					}
				}
				if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
					return true; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
			}
		}
	}
	return false;
}



void CurrentScore(UserInfo* userInfo)
{
	if (userInfo->line >= 2)
	{
		userInfo->score += (userInfo->line + 1) * 10;
		moveTo(userInfo->fd, 14, 2 * COL + 2);
		color(userInfo->fd, 7);
		//printf("Score:%d", grade);
		outputgrade(userInfo->fd, "Score: ", userInfo->score);
	}

	else
	{
		userInfo->score += userInfo->line * 10;
		moveTo(userInfo->fd, 14, 2 * COL + 2);
		color(userInfo->fd, 7);
		outputgrade(userInfo->fd, "Score: ", userInfo->score);
	}
}

void clear(int client)
{
	int i;
	string emptyLine(4 * COL, ' ');
	for (i = 1; i <= ROW; i++)


	{
		moveTo(client, i, 1);
		//cout << emptyLine;
		output(client, emptyLine);
	}
}

void HandleClientConnection(int serverSocket, unordered_map<int, UserInfo*>& p, int epollfd)
{
	int clientSocket = accept(serverSocket, NULL, NULL);
	if (clientSocket == -1)
	{
		std::cerr << "Failed to accept client connection" << std::endl;
		return;
	}
	else
	{
		printf("Client[%d], welcome!\n", clientSocket);
		//Client.push_back(client);
	}

	// 设置客户端连接为非阻塞模式
	SetSocketBlocking(clientSocket, false);

	// 创建新的用户信息结构体
	UserInfo* newUser = new UserInfo;
	newUser->fd = clientSocket;
	newUser->line = 0;
	newUser->score = 0;

	p.insert(make_pair(newUser->fd, newUser));

	// 将新连接的事件添加到 epoll 实例中
	struct epoll_event newEvent;
	newEvent.events = EPOLLIN | EPOLLET; // 监听读事件并将EPOLL设为边缘触发(Edge Triggered)模式，
	newEvent.data.ptr = newUser; // 将指针指向用户信息结构体

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
	{
		std::cerr << "Failed to add new client event to epoll instance." << std::endl;
		close(clientSocket);
		delete newUser;
		return;
	}

	InitInterface(newUser); //初始化界面
	InitBlockInfo(); //初始化方块信息
	srand((unsigned int)time(NULL)); //设置随机数生成的起点
	newUser->shape = rand() % 7;
	newUser->form = rand() % 4; //随机获取方块的形状和形态
	newUser->nextShape = rand() % 7;
	newUser->nextForm = rand() % 4;
	//随机获取下一个方块的形状和形态
	newUser->row = 1;
	newUser->col = COL / 2 - 1; //方块初始下落位置
	color(clientSocket, newUser->nextShape); //颜色设置为下一个方块的颜色
	DrawBlock(clientSocket, newUser->nextShape, newUser->nextForm, 3, COL + 3); //将下一个方块显示在右上角
	color(clientSocket, newUser->shape); //颜色设置为当前正在下落的方块
	DrawBlock(clientSocket, newUser->shape, newUser->form, newUser->row, newUser->col); //将该方块显示在初始下落位置
}

// 定义处理用户逻辑的函数
void processUserLogic(UserInfo* user, int epollfd) {
	if (IsLegal(user, user->shape, user->form, user->row + 1, user->col) == 0) {
		// Store the current block's information in the 'user' object
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if (block[user->shape][user->form].space[i][j] == 1) {
					user->data[user->row + i - 1][user->col + j - 1] = 1;
					user->color[user->row + i - 1][user->col + j - 1] = user->shape;
				}
			}
		}

		user->line = 0;
		while (JudeScore(user));
		CurrentScore(user);

		int flag = 0;

		//判断游戏是否结束
		for (int j = 1; j < COL - 1; j++)
		{
			if (user->data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
			{
				flag = 1;
				sleep(1); //留给玩家反应时间
				color(user->fd, 7); //颜色设置为白色
				moveTo(user->fd, ROW / 2, 2 * (COL / 3));
				output(user->fd, "GAME OVER");

				moveTo(user->fd, ROW / 2 + 3, 2 * (COL / 3));
				output(user->fd, "Start Again ? (y/n):");

				// 将客户端套接字设置为阻塞模式
				SetSocketBlocking(user->fd, true);

				char buf[BUFFSIZE] = { 0 }; // 用于收发数据
				recv(user->fd, buf, sizeof(buf), 0);

				if (*buf == 'Y' || *buf == 'y')
				{
					clear(user->fd);


					user->score = 0;

					InitInterface(user); //初始化界面

					user->shape = rand() % 7;
					user->form = rand() % 4; //随机获取方块的形状和形态
					user->nextShape = rand() % 7;
					user->nextForm = rand() % 4;
					//随机获取下一个方块的形状和形态
					user->row = 1;
					user->col = COL / 2 - 1; //方块初始下落位置
					color(user->fd, user->nextShape); //颜色设置为下一个方块的颜色
					DrawBlock(user->fd, user->nextShape, user->nextForm, 3, COL + 3); //将下一个方块显示在右上角
					color(user->fd, user->shape); //颜色设置为当前正在下落的方块
					DrawBlock(user->fd, user->shape, user->form, user->row, user->col); //将该方块显示在初始下落位置

					return;

				}
				else if (*buf == 'n' || *buf == 'N')
				{
					printf("Client[%d] disconnected!\n", user->fd);
					qiut.push_back(user->fd);
					close(user->fd);
					//close(serverSocket);
					delete user;
					epoll_ctl(epollfd, EPOLL_CTL_DEL, user->fd, nullptr);
					return;
				}
				else
				{
					moveTo(user->fd, ROW / 2 + 4, 2 * (COL / 3));
					//printf("选择错误，请再次选择");
					output(user->fd, "选择错误，请再次选择");
				}

			}
		}



		if (!flag)
		{
			user->shape = user->nextShape;
			user->form = user->nextForm;
			DrawSpace(user->fd, user->nextShape, user->nextForm, 3, COL + 3);
			user->nextShape = rand() % 7;
			user->nextForm = rand() % 4;

			user->row = 1;
			user->col = COL / 2 - 1;
			color(user->fd, user->nextShape);
			DrawBlock(user->fd, user->nextShape, user->nextForm, 3, COL + 3);
			color(user->fd, user->shape);
			DrawBlock(user->fd, user->shape, user->form, user->row, user->col);
		}

	}
	else {
		DrawSpace(user->fd, user->shape, user->form, user->row, user->col);
		user->row++;
		DrawBlock(user->fd, user->shape, user->form, user->row, user->col);
	}
}

void processTimerEvent(int timerfd, vector<int>& qiut, unordered_map<int, UserInfo*>& p, int epollfd) {
	// 读取 timerfd，重置定时器
	uint64_t expirations;
	if (read(timerfd, &expirations, sizeof(expirations)) == -1) {
		cout << "Failed to read expirations” << endl";
		return;
	}
	if (!p.empty())
	{
		for (auto i = p.begin(); i != p.end(); i++) {
			if (p.empty())
			{
				return;
			}

			//thread t(processUserLogic, i->second, epollfd);
			processUserLogic(i->second, epollfd);
			//t.detach();
		}

		for (auto i = qiut.begin(); i != qiut.end(); i++)
		{
			p.erase(*i);
		}
		qiut.clear();
	}
}

void handleClientData(int epollfd, UserInfo* userInfo)
{
	// 处理已连接客户端的数据接收事件
	char buffer[1024];
	int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
	if (bytesRead == -1) {
		std::cerr << "Failed to receive data from client" << std::endl;
	}
	else if (bytesRead == 0) {
		// 客户端连接已关闭
		std::cout << "Client disconnected" << std::endl;
		close(userInfo->fd);
		epoll_ctl(epollfd, EPOLL_CTL_DEL, userInfo->fd, nullptr);
	}
	else
	{
		// 处理接收到的数据
		string temp = buffer;
		if (temp == KEY_DOWN)//下
		{
			if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row + 1, userInfo->col) == 1) //判断方块向下移动一位后是否合法
			{
				//方块下落后合法才进行以下操作
				DrawSpace(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
				userInfo->row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）
				DrawBlock(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
			}
		}
		else if (temp == KEY_LEFT)//左
		{
			if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col - 1) == 1) //判断方块向左移动一位后是否合法
			{
				//方块左移后合法才进行以下操作
				DrawSpace(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);//用空格覆盖当前方块所在位置
				userInfo->col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）
				DrawBlock(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
			}
		}
		else if (temp == KEY_RIGHT)//右
		{
			if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col + 1) == 1) //判断方块向右移动一位后是否合法
			{
				//方块右移后合法才进行以下操作
				DrawSpace(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
				userInfo->col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）
				DrawBlock(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
			}
		}
		else
		{
			if (IsLegal(userInfo, userInfo->shape, (userInfo->form + 1) % 4, userInfo->row + 1, userInfo->col) == 1) //判断方块旋转后是否合法
			{
				//方块旋转后合法才进行以下操作
				DrawSpace(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
				userInfo->row++; //纵坐标自增（总不能原地旋转吧）
				userInfo->form = (userInfo->form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）
				DrawBlock(userInfo->fd, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
			}
		}
	}
}

void processEvents(int readyCount, epoll_event* events, int serverSocket, int timerfd, vector<int>& qiut, unordered_map<int, UserInfo*>& p, int epollfd)
{
	for (int i = 0; i < readyCount; ++i)
	{
		UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
		int currentFd = events[i].data.fd;

		if (currentFd == serverSocket)
		{
			HandleClientConnection(serverSocket, p, epollfd);
		}
		else if (currentFd == timerfd)
		{
			processTimerEvent(timerfd, qiut, p, epollfd);
		}
		else
		{
			handleClientData(epollfd, userInfo);
		}
	}
}

void InitInterface(UserInfo* user)
{
	for (int i = 0; i < ROW; i++)
	{
		for (int j = 0; j < COL + 10; j++)
		{
			if (j == 0 || j == COL - 1 || j == COL + 9)
			{
				user->data[i][j] = 1; //标记该位置有方块
				moveTo(user->fd, i + 1, 2 * (j + 1) - 1);
				output(user->fd, "■");
			}
			else if (i == ROW - 1)
			{
				user->data[i][j] = 1; //标记该位置有方块
				moveTo(user->fd, i + 1, 2 * (j + 1) - 1);
				output(user->fd, "■");
			}
			else
				user->data[i][j] = 0; //标记该位置无方块
		}
	}

	for (int i = COL; i < COL + 10; i++)
	{
		user->data[11][i] = 1; //标记该位置有方块
		moveTo(user->fd, 11 + 1, 2 * i + 1);
		output(user->fd, "■");
	}

	moveTo(user->fd, 2, 2 * COL + 1 + 1);
	ChangeCurrentColor(user->fd, COLOR_WHITE);
	output(user->fd, "Next:");

	moveTo(user->fd, 14, 2 * COL + 2);
	ChangeCurrentColor(user->fd, COLOR_WHITE);
	outputgrade(user->fd, "Score: ", user->score);
}



int main()
{
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		std::cerr << "Failed to create socket" << std::endl;
		return -1;
	}
	// 对应伪代码中的bind(sockfd, ip::port和一些配置);
	struct sockaddr_in addr;    // 用于存放ip和端口的结构
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(DEFAULT_PORT);
	if (-1 == bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)))
	{
		printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
		return -1;
	}
	// 对应伪代码中的listen(sockfd);    
	if (-1 == listen(serverSocket, MAXLINK))
	{
		printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
		return -1;
	}

	int epollfd = epoll_create(MAXSIZE);
	if (epollfd < 0)	//创建epoll实例
	{
		printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
		exit(-1);
	}

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = serverSocket;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &event) == -1)
	{
		std::cerr << "Failed to add server socket to epoll" << std::endl;
		close(serverSocket);
		close(epollfd);
		return -1;
	}

	// 创建定时器
	int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
	struct itimerspec timer_spec;
	timer_spec.it_interval.tv_sec = 1;  // 一秒触发一次
	timer_spec.it_interval.tv_nsec = 0;
	timer_spec.it_value.tv_sec = 1;     // 初始延时一秒
	timer_spec.it_value.tv_nsec = 0;
	timerfd_settime(timerfd, 0, &timer_spec, NULL);


	// 将定时器加入 epoll 监听集合
	struct epoll_event timer_event;
	timer_event.events = EPOLLIN;
	timer_event.data.fd = timerfd;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, timerfd, &timer_event) == -1)
	{
		std::cerr << "Failed to add timerfd to epoll" << std::endl;
		return 1;
	}

	printf("======waiting for client's request======\n");

	while (1)
	{
		struct epoll_event events[MAXSIZE];
		int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
		if (readyCount == -1) {
			std::cerr << "Failed on epoll_wait" << std::endl;
			continue;
		}
		processEvents(readyCount, events, serverSocket, timerfd, qiut, p, epollfd);

	}
	return 0;
}