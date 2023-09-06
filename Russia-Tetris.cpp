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
#include<map>
#include <algorithm>
#include<vector>
#include <thread>
#define MAXSIZE 2048
#define DEFAULT_PORT 9998// 指定端口为9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define ROW 24 //游戏区行数
#define COL 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

using namespace std;

struct UserInfo
{
	int epollfd;
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
};


map<int, UserInfo*> p;

vector<int> qiut;

vector<UserInfo*>overuser;

struct Block
{
	int space[4][4];
};

Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

enum Shape
{
	SHAPE_T = 0,
	SHAPE_L = 1,
	SHAPE_J = 2,
	SHAPE_Z = 3,
	SHAPE_S = 4,
	SHAPE_O = 5,
	SHAPE_I = 6
};

enum Color
{
	COLOR_PURPLE = 35,
	COLOR_RED = 31,
	COLOR_LOWBLUE = 36,
	COLOR_YELLO = 33,
	COLOR_DEEPBLUE = 34,
	COLOR_WHITE = 37
};



/*
//void output(int client, string s);//输出到客户端函数

void outputgrade(int client, string s, int grade);//输出分数到客户端

void moveTo(int client, int row, int col);//打印移动函数

void ChangeCurrentColor(int client, int n);//更改目前字体及方块颜色函数

void SetSocketBlocking(int socket, bool blocking);//将客户端套接字设置为阻塞与非阻塞

void InitBlockInfo();//初始化方块信息

void DrawBlock(int client, int shape, int form, int row, int col);//画出方块

void DrawSpace(int client, int shape, int form, int row, int col);//空格覆盖

void color(int client, int c);//颜色设置

bool IsLegal(UserInfo* user, int shape, int form, int row, int col);//合法性判断

bool Is_Increase_Score(UserInfo* userInfo);//判断是否得分

void UpdateCurrentScore(UserInfo* userInfo);//更新目前得分

void clear(int client);//清屏函数

bool IsOver(UserInfo* user);//判断游戏是否结束

void showover(UserInfo* user);//输出游戏结束界面到客户端

void HandleClientConnection(int serverSocket, int epollfd);//处理客户端连接

void processUserLogic(UserInfo* user);//判断方块下降逻辑函数

void processTimerEvent();//处理各个客户端的方块下降函数

void handleClientData(int epollfd, UserInfo* userInfo);//处理各个客户端发来的信息并及时响应

void processEvents(int readyCount, epoll_event* events, int serverSocket, int timerfd, int epollfd);
*/
void InitInterface(UserInfo* user);//初始化界面



bool output(UserInfo* user, string s) {

	int bytesSent = send(user->fd, s.c_str(), s.length(), 0);
	if (bytesSent == -1) {
		qiut.push_back(user->fd);
		cerr << "Failed to send data to client" << "[" << user->fd << "]" << endl;
		printf("Client[%d] disconnected!\n", user->fd);
		close(user->fd);
		delete user;
		epoll_ctl(user->epollfd, EPOLL_CTL_DEL, user->fd, nullptr);
		return false;
	}
	return true;
}

void outputgrade(UserInfo* user, string s, int grade) {
	// 检查输入是否合法
	if (grade < 0) {
		cerr << "Invalid grades: " << grade << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return;
	}

	string command = s + to_string(grade);
	if (!output(user, command))
		return;
}

void moveTo(UserInfo* user, int row, int col) {
	// 检查输入是否合法
	if (row < 0 || col < 0) {
		cerr << "Invalid row or col number" << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return;
	}

	string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
	if (!output(user, command))
		return;
}

void ChangeCurrentColor(UserInfo* user, int n) {
	// 检查输入是否合法
	if (n < 0 || n > 255) {
		cerr << "Invalid color number" << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return;
	}

	string command = "\33[" + to_string(n) + "m";
	if (!output(user, command))
		return;
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
	auto& spaceT = blockDefines[SHAPE_T][0].space;
	for (i = 0; i <= 2; i++)
	{
		spaceT[1][i] = 1;
	}
	spaceT[2][1] = 1;

	//“L”形
	auto& spaceL = blockDefines[SHAPE_L][0].space;
	for (i = 1; i <= 3; i++)
	{
		spaceL[i][1] = 1;
	}
	spaceL[3][2] = 1;

	//“J”形
	auto& spaceJ = blockDefines[SHAPE_J][0].space;
	for (i = 1; i <= 3; i++)
	{
		spaceJ[i][2] = 1;
	}
	spaceJ[3][1] = 1;

	for (int i = 0; i <= 1; i++)
	{
		//“Z”形
		auto& spaceZ = blockDefines[SHAPE_Z][0].space;
		spaceZ[1][i] = 1;
		spaceZ[2][i + 1] = 1;
		//“S”形
		auto& spaceS = blockDefines[SHAPE_S][0].space;
		spaceS[1][i + 1] = 1;
		spaceS[2][i] = 1;
		//“O”形
		auto& spaceO = blockDefines[SHAPE_O][0].space;
		spaceO[1][i + 1] = 1;
		spaceO[2][i + 1] = 1;
	}

	//“I”形
	auto& spaceI = blockDefines[SHAPE_I][0].space;
	for (i = 0; i <= 3; i++)
	{
		spaceI[i][1] = 1;
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
					temp[i][j] = blockDefines[shape][form].space[i][j];
				}
			}
			//将第form种形态顺时针旋转，得到第form+1种形态
			for (i = 0; i < 4; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					blockDefines[shape][form + 1].space[i][j] = temp[3 - j][i];
				}
			}
		}
	}
}

//画出方块
void DrawBlock(UserInfo* user, int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(user, row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("■"); //输出方块
				if (!output(user, "■"))
					return;
			}
		}
	}
}

//空格覆盖
//无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
//而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
//在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

void DrawSpace(UserInfo* user, int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				moveTo(user, row + i, 2 * (col + j) - 1);//光标跳转到指定位置
				//printf("  ");//打印空格覆盖（两个空格）
				if (!output(user, "  "))
					return;
			}
		}

	}
}

//颜色设置
void color(UserInfo* user, int c)
{
	switch (c)
	{
	case SHAPE_T:
		ChangeCurrentColor(user, COLOR_PURPLE); //“T”形方块设置为紫色
		break;
	case SHAPE_L:
	case SHAPE_J:
		ChangeCurrentColor(user, COLOR_RED); //“L”形和“J”形方块设置为红色
		break;
	case SHAPE_Z:
	case SHAPE_S:
		ChangeCurrentColor(user, COLOR_LOWBLUE);//“Z”形和“S”形方块设置为浅蓝色
		break;
	case SHAPE_O:
		ChangeCurrentColor(user, COLOR_YELLO);//“O”形方块设置为黄色
		break;
	case SHAPE_I:
		ChangeCurrentColor(user, COLOR_DEEPBLUE);//“I”形方块设置为深蓝色
		break;
	default:
		ChangeCurrentColor(user, COLOR_WHITE); //其他默认设置为白色
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
			if ((blockDefines[shape][form].space[i][j] == 1) && (user->data[row + i - 1][col + j - 1] == 1))
				return false;
		}
	}
	return true;
}

//判断得分与结束

// 判断得分
//从下往上判断，若某一行方块全满，则将改行方块数据清空，并将该行上方的方块全部下移，下移结束后返回1，表示还需再次调用该函数进行判断
//因为被下移的行并没有进行判断，可能还存在满行。

bool Is_Increase_Score(UserInfo* userInfo)
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
				moveTo(userInfo, i + 1, 2 * j + 1);
				//printf("  ");
				if (!output(userInfo, "  "))
					return false;

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
						moveTo(userInfo, m + 1, 2 * n + 1);
						color(userInfo, userInfo->color[m][n]);//颜色设置为还方块的颜色
						//printf("■"); //打印方块
						if (!output(userInfo, "■"))
							return false;
					}
					else
					{
						moveTo(userInfo, m + 1, 2 * n + 1);
						//printf("  ");
						if (!output(userInfo, "  "))
							return false;
					}
				}
				if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
					return true; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
			}
		}
	}
	return false;
}

void UpdateCurrentScore(UserInfo* userInfo)
{
	if (userInfo->line >= 2)
	{
		userInfo->score += (userInfo->line + 1) * 10;
		moveTo(userInfo, 14, 2 * COL + 2);
		color(userInfo, 7);
		//printf("Score:%d", grade);
		outputgrade(userInfo, "Score: ", userInfo->score);
	}
	else
	{
		userInfo->score += userInfo->line * 10;
		moveTo(userInfo, 14, 2 * COL + 2);
		color(userInfo, 7);
		outputgrade(userInfo, "Score: ", userInfo->score);
	}
}

void clear(UserInfo* user)
{
	int i;
	string emptyLine(4 * COL, ' ');
	for (i = 1; i <= ROW; i++)
	{
		moveTo(user, i, 1);
		//cout << emptyLine;
		if (!output(user, emptyLine))
			return;
	}
}

//判断结束
//直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
//游戏结束后询问玩家是否再来一局。

bool IsOver(UserInfo* user)
{
	//判断游戏是否结束
	for (int j = 1; j < COL - 1; j++)
	{
		if (user->data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
		{
			return true;
		}
	}
	return false;
}

void showover(UserInfo* user)
{
	//sleep(1); //留给玩家反应时间
	color(user, 7); //颜色设置为白色
	moveTo(user, ROW / 2, 2 * (COL / 3));
	if (!output(user, "GAME OVER"))
		return;
	moveTo(user, ROW / 2 + 3, 2 * (COL / 3));
	if (!output(user, "Start Again ? (y/n):"))
		return;

	qiut.push_back(user->fd);

	overuser.push_back(user);
}

void HandleClientConnection(int serverSocket, int epollfd)
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
	newUser->epollfd = epollfd;

	p.insert(make_pair(newUser->fd, newUser));

	// 将新连接的事件添加到 epoll 实例中
	struct epoll_event newEvent;
	newEvent.events = EPOLLIN | EPOLLET; // 监听读事件并将EPOLL设为边缘触发(Edge Triggered)模式，
	newEvent.data.ptr = newUser; // 将指针指向用户信息结构体

	if (epoll_ctl(newUser->epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
	{
		std::cerr << "Failed to add new client event to epoll instance." << std::endl;
		close(clientSocket);
		p.erase(newUser->fd);
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
	color(newUser, newUser->nextShape); //颜色设置为下一个方块的颜色
	DrawBlock(newUser, newUser->nextShape, newUser->nextForm, 3, COL + 3); //将下一个方块显示在右上角
	color(newUser, newUser->shape); //颜色设置为当前正在下落的方块
	DrawBlock(newUser, newUser->shape, newUser->form, newUser->row, newUser->col); //将该方块显示在初始下落位置
}

// 定义处理用户逻辑的函数
void processUserLogic(UserInfo* user)
{
	if (IsLegal(user, user->shape, user->form, user->row + 1, user->col) == 0) {
		// Store the current block's information in the 'user' object
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if (blockDefines[user->shape][user->form].space[i][j] == 1) {
					user->data[user->row + i - 1][user->col + j - 1] = 1;
					user->color[user->row + i - 1][user->col + j - 1] = user->shape;
				}
			}
		}

		user->line = 0;
		while (Is_Increase_Score(user));
		UpdateCurrentScore(user);
		if (!IsOver(user))//判断是否结束
		{
			user->shape = user->nextShape;
			user->form = user->nextForm;
			DrawSpace(user, user->nextShape, user->nextForm, 3, COL + 3);
			user->nextShape = rand() % 7;
			user->nextForm = rand() % 4;

			user->row = 1;
			user->col = COL / 2 - 1;
			color(user, user->nextShape);
			DrawBlock(user, user->nextShape, user->nextForm, 3, COL + 3);
			color(user, user->shape);
			DrawBlock(user, user->shape, user->form, user->row, user->col);
		}
		else
		{
			showover(user);
		}
	}
	else {
		DrawSpace(user, user->shape, user->form, user->row, user->col);
		user->row++;
		DrawBlock(user, user->shape, user->form, user->row, user->col);
	}
}

void processTimerEvent()
{
	// 记录上次触发时间
	static std::chrono::steady_clock::time_point lastTriggerTime = std::chrono::steady_clock::now();

	// 获取当前时间
	std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();

	// 计算距离上次触发经过的时间
	std::chrono::duration<double> elapsed_time = currentTime - lastTriggerTime;

	// 如果时间差大于等于1秒
	if (elapsed_time >= std::chrono::seconds(1))
	{
		// 执行相应的逻辑处理

		if (!p.empty())
		{
			for (auto i = p.begin(); i != p.end(); i++) //遍历客户端
			{
				processUserLogic(i->second);
			}

			for (auto i = qiut.begin(); i != qiut.end(); i++)
			{
				p.erase(*i);
			}
			qiut.clear();
		}

		// 将上次触发时间更新为当前时间
		lastTriggerTime = currentTime;
	}

}

void handleClientData(UserInfo* userInfo)
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
		p.erase(userInfo->fd);
		close(userInfo->fd);
		epoll_ctl(userInfo->epollfd, EPOLL_CTL_DEL, userInfo->fd, nullptr);
	}
	else
	{
		auto it = find(overuser.begin(), overuser.end(), userInfo);
		if (it == overuser.end())
		{
			// 处理接收到的数据
			if (strcmp(buffer, KEY_DOWN) == 0)//下
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row + 1, userInfo->col) == 1) //判断方块向下移动一位后是否合法
				{
					//方块下落后合法才进行以下操作
					DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
					userInfo->row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）
					DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
				}
			}
			else if (strcmp(buffer, KEY_LEFT) == 0)//左
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col - 1) == 1) //判断方块向左移动一位后是否合法
				{
					//方块左移后合法才进行以下操作
					DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);//用空格覆盖当前方块所在位置
					userInfo->col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）
					DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
				}
			}
			else if (strcmp(buffer, KEY_RIGHT) == 0)//右
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col + 1) == 1) //判断方块向右移动一位后是否合法
				{
					//方块右移后合法才进行以下操作
					DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
					userInfo->col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）
					DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
				}
			}
			else if (*buffer == ' ')
			{
				if (IsLegal(userInfo, userInfo->shape, (userInfo->form + 1) % 4, userInfo->row + 1, userInfo->col) == 1) //判断方块旋转后是否合法
				{
					//方块旋转后合法才进行以下操作
					DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //用空格覆盖当前方块所在位置
					userInfo->row++; //纵坐标自增（总不能原地旋转吧）
					userInfo->form = (userInfo->form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）
					DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col);
				}
			}
		}
		else
		{
			if (*buffer == 'Y' || *buffer == 'y')
			{
				overuser.erase(it);

				p.insert(make_pair(userInfo->fd, userInfo));

				clear(userInfo);

				userInfo->score = 0;

				InitInterface(userInfo); //初始化界面

				userInfo->shape = rand() % 7;
				userInfo->form = rand() % 4; //随机获取方块的形状和形态
				userInfo->nextShape = rand() % 7;
				userInfo->nextForm = rand() % 4;
				//随机获取下一个方块的形状和形态
				userInfo->row = 1;
				userInfo->col = COL / 2 - 1; //方块初始下落位置
				color(userInfo, userInfo->nextShape); //颜色设置为下一个方块的颜色
				DrawBlock(userInfo, userInfo->nextShape, userInfo->nextForm, 3, COL + 3); //将下一个方块显示在右上角
				color(userInfo, userInfo->shape); //颜色设置为当前正在下落的方块
				DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col); //将该方块显示在初始下落位置

			}
			else if (*buffer == 'n' || *buffer == 'N')
			{
				overuser.erase(it);

				printf("Client[%d] disconnected!\n", userInfo->fd);
				//qiut.push_back(userInfo->fd);
				close(userInfo->fd);
				//close(serverSocket);
				delete userInfo;
				epoll_ctl(userInfo->epollfd, EPOLL_CTL_DEL, userInfo->fd, nullptr);
			}
			else
			{
				moveTo(userInfo, ROW / 2 + 4, 2 * (COL / 3));
				//printf("选择错误，请再次选择");
				if (!output(userInfo, "选择错误，请再次选择"))
					return;
			}
		}
	}
}

void processEvents(int readyCount, epoll_event* events, int timerfd)
{
	for (int i = 0; i < readyCount; ++i)
	{
		UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
		int currentFd = events[i].data.fd;

		if (currentFd == timerfd)
		{
			processTimerEvent();
		}
		else
		{
			handleClientData(userInfo);
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
				moveTo(user, i + 1, 2 * (j + 1) - 1);
				if (!output(user, "■"))
					return;
			}
			else if (i == ROW - 1)
			{
				user->data[i][j] = 1; //标记该位置有方块
				moveTo(user, i + 1, 2 * (j + 1) - 1);
				if (!output(user, "■"))
					return;
			}
			else
				user->data[i][j] = 0; //标记该位置无方块
		}
	}

	for (int i = COL; i < COL + 10; i++)
	{
		user->data[11][i] = 1; //标记该位置有方块
		moveTo(user, 11 + 1, 2 * i + 1);
		if (!output(user, "■"))
			return;
	}

	moveTo(user, 2, 2 * COL + 1 + 1);
	ChangeCurrentColor(user, COLOR_WHITE);
	if (!output(user, "Next:"))
		return;

	moveTo(user, 14, 2 * COL + 2);
	ChangeCurrentColor(user, COLOR_WHITE);
	outputgrade(user, "Score: ", user->score);
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
	timer_spec.it_interval.tv_sec = 0;  // 避免重复触发
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

	HandleClientConnection(serverSocket, epollfd);

	while (1)
	{
		struct epoll_event events[MAXSIZE];
		int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
		if (readyCount == -1) {
			std::cerr << "Failed on epoll_wait" << std::endl;
			continue;
		}
		processEvents(readyCount, events, timerfd);

	}
	return 0;
}
