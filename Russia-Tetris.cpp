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
#include<map>
#include <algorithm>
#include<vector>
#include <signal.h>
#include <thread>
#define MAXSIZE 2048
#define DEFAULT_PORT 9997// 指定端口为9999
#define BUFFSIZE 2048
#define MAXLINK 2048

#define WINDOW_ROW_COUNT 24 //游戏区行数
#define WINDOW_COL_COUNT 20 //游戏区列数

#define KEY_DOWN "\x1b[B" //方向键：下
#define KEY_LEFT "\x1b[D" //方向键：左
#define KEY_RIGHT "\x1b[C" //方向键：右

using namespace std;

enum GameStatus
{
	STATUS_PLAYING = 1,
	STATUS_OVER_CONFIRMING = 0,
	STATUS_OVER_QUIT = -1
};

struct UserInfo
{
	int status;
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

	int data[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];//用于标记指定位置是否有方块（1为有，0为无）
	int color[WINDOW_ROW_COUNT][WINDOW_COL_COUNT + 10];//用于记录指定位置的方块颜色编码
};

map<int, UserInfo*> g_playing_gamer;

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

bool output(UserInfo* user, string s)
{
	int bytesSent = send(user->fd, s.c_str(), s.length(), 0);
	if (bytesSent == -1)
	{
		user->status = STATUS_OVER_QUIT;
		close(user->fd);
		printf("Client[%d] send Error: %s (errno: %d)\n", user->fd, strerror(errno), errno);
		return false;
	}
	else if (bytesSent == 0) {
		// 客户端连接已关闭
		user->status = STATUS_OVER_QUIT;
		close(user->fd);
		return false;
	}
	return true;
}

bool moveTo(UserInfo* user, int row, int col) {
	// 检查输入是否合法
	if (row < 0 || col < 0) {
		cerr << "Invalid row or col number" << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return false;
	}

	string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
	if (!output(user, command))
		return false;
	return true;
}

bool ChangeCurrentColor(UserInfo* user, int n) {
	// 检查输入是否合法
	if (n < 0 || n > 255) {
		cerr << "Invalid color number" << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return false;
	}

	string command = "\33[" + to_string(n) + "m";
	if (!output(user, command))
		return false;
	return true;
}

bool outputgrade(UserInfo* user, int row, int col, int n, string s, int grade)
{
	// 检查输入是否合法
	if (grade < 0) {
		cerr << "Invalid grades: " << grade << endl;
		// 可以根据具体情况进行相应的错误处理操作
		return false;
	}

	if (!moveTo(user, row, col))
	{
		return false;
	}
	if (!ChangeCurrentColor(user, n))
	{
		return false;
	}

	string command = s + to_string(grade);

	if (!output(user, command))
	{
		return false;
	}
	return true;
}

bool outputText(UserInfo* user, int row, int col, int n, string s)
{
	if (!moveTo(user, row, col))
	{
		return false;
	}
	if (!ChangeCurrentColor(user, n))
	{
		return false;
	}
	if (!output(user, s))
	{
		return false;
	}
	return true;
}

bool InitInterface(UserInfo* user)
{
	for (int i = 0; i < WINDOW_ROW_COUNT; i++)
	{
		for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
		{
			if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
			{
				user->data[i][j] = 1; //标记该位置有方块

				if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
					return false;
			}
			else if (i == WINDOW_ROW_COUNT - 1)
			{
				user->data[i][j] = 1; //标记该位置有方块

				if (!outputText(user, i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
					return false;
			}
			else
				user->data[i][j] = 0; //标记该位置无方块
		}
	}

	for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
	{
		user->data[11][i] = 1; //标记该位置有方块
		if (!outputText(user, 11 + 1, 2 * i + 1, COLOR_WHITE, "■"))
			return false;
	}

	if (!outputText(user, 2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
		return false;


	if (!outputText(user, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
		return false;

	return true;
}


bool SuccessSetSocketBlocking(int socket, bool blocking) {
	// 获取套接字标志
	int flags = fcntl(socket, F_GETFL, 0);
	if (flags < 0) {
		close(socket);
		std::cerr << "Failed to get socket flags" << std::endl;
		return false;
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
		close(socket);
		std::cerr << "Failed to set socket mode" << std::endl;
		return false;
	}
	return true;
}

//颜色设置
int color(int c)
{
	switch (c)
	{
	case SHAPE_T:
		return COLOR_PURPLE;//“T”形方块设置为紫色
	case SHAPE_L:
	case SHAPE_J:
		return COLOR_RED;//“L”形和“J”形方块设置为红色
	case SHAPE_Z:
	case SHAPE_S:
		return COLOR_LOWBLUE;//“Z”形和“S”形方块设置为浅蓝色
	case SHAPE_O:
		return COLOR_YELLO;//“O”形方块设置为黄色
	case SHAPE_I:
		return COLOR_DEEPBLUE;//“I”形方块设置为深蓝色
	default:
		return COLOR_WHITE;
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
bool DrawBlock(UserInfo* user, int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				if (!outputText(user, row + i, 2 * (col + j) - 1, color(shape), "■"))
					return false;
			}
		}
	}
	return true;
}

//空格覆盖
//无论是游戏区方块的移动，还是提示区右上角下一个方块的显示，都需要方块位置的变换.
//而在变化之前肯定是要先将之前打印的方块用空格进行覆盖，然后再打印变化后的方块
//在覆盖方块时特别需要注意的是，要覆盖一个小方块需要用两个空格。

bool DrawSpace(UserInfo* user, int shape, int form, int row, int col)
{
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
			{
				if (!outputText(user, row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
					return false;
			}
		}
	}
	return true;
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

int Is_Increase_Score(UserInfo* userInfo)
{
	int i = 0, j = 0;
	//判断是否得分
	for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
	{
		int sum = 0;
		for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
		{
			sum += userInfo->data[i][j];
		}
		if (sum == 0)
			break;
		if (sum == WINDOW_COL_COUNT - 2)//该行全是方块，可得分
		{
			userInfo->line++;
			for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//清除得分行的方块信息
			{
				userInfo->data[i][j] = 0;
				if (!outputText(userInfo, i + 1, 2 * j + 1, COLOR_WHITE, "  "))
					return -1;

			}
			//把被清除行上面的行整体向下挪一格
			for (int m = i; m > 1; m--)
			{
				sum = 0;//记录上一行的方块个数
				for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
				{
					sum += userInfo->data[m - 1][n];//统计上一行的方块个数
					userInfo->data[m][n] = userInfo->data[m - 1][n];//将上一行方块的标识移到下一行
					userInfo->color[m][n] = userInfo->color[m - 1][n];//将上一行方块的颜色编号移到下一行
					if (userInfo->data[m][n] == 1)
					{
						if (!outputText(userInfo, m + 1, 2 * n + 1, color(userInfo->color[m][n]), "■"))
							return -1;
					}
					else
					{
						if (!outputText(userInfo, m + 1, 2 * n + 1, COLOR_WHITE, "  "))
							return -1;
					}
				}
				if (sum == 0) //上一行移下来的全是空格，无需再将上层的方块向下移动（移动结束）
					return 1; //返回1，表示还需调用该函数进行判断（移动下来的可能还有满行）
			}
		}
	}
	return 0;
}

bool UpdateCurrentScore(UserInfo* userInfo)
{
	if (userInfo->line >= 2)
	{
		userInfo->score += (userInfo->line + 1) * 10;
		if (!outputgrade(userInfo, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", userInfo->score))
		{
			return false;
		}
	}
	else
	{
		userInfo->score += userInfo->line * 10;
		if (!outputgrade(userInfo, 14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", userInfo->score))
		{
			return false;
		}
	}
	return true;
}

bool clear(UserInfo* user)
{
	int i;
	string emptyLine(4 * WINDOW_COL_COUNT, ' ');
	for (i = 1; i <= WINDOW_ROW_COUNT; i++)
	{
		if (!outputText(user, i, 1, COLOR_WHITE, emptyLine))
			return false;
	}
	return false;
}

//判断结束
//直接判断游戏区最上面的一行当中是否有方块存在，若存在方块，则游戏结束。
//游戏结束后询问玩家是否再来一局。

bool IsOver(UserInfo* user)
{
	//判断游戏是否结束
	for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
	{
		if (user->data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
		{
			user->status = STATUS_OVER_CONFIRMING;
			return true;
		}
	}
	return false;
}

bool showover(UserInfo* user)
{
	if (!outputText(user, WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
		return false;

	if (!outputText(user, WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
		return false;

	return true;
}

void handleNewClientConnection(int serverSocket, int epollfd)
{
	int clientSocket = accept(serverSocket, NULL, NULL);
	if (clientSocket == -1)
	{
		printf("accept Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
		return;
	}
	else
	{
		printf("Client[%d], welcome!\n", clientSocket);
		//Client.push_back(client);
	}

	// 设置客户端连接为非阻塞模式
	if (!SuccessSetSocketBlocking(clientSocket, false))
		return;

	// 创建新的用户信息结构体
	UserInfo* newUser = new UserInfo;

	if (newUser == nullptr) {
		close(clientSocket);
		printf("allocate memory for newUser Error In handleNewClientConnection");
		return;
	}

	newUser->fd = clientSocket;
	newUser->line = 0;
	newUser->score = 0;
	newUser->status = STATUS_PLAYING;
	newUser->epollfd = epollfd;

	g_playing_gamer.insert(make_pair(newUser->fd, newUser));

	// 将新连接的事件添加到 epoll 实例中
	struct epoll_event newEvent;
	newEvent.events = EPOLLIN | EPOLLET; // 监听读事件并将EPOLL设为边缘触发(Edge Triggered)模式，
	newEvent.data.ptr = newUser; // 将指针指向用户信息结构体

	if (epoll_ctl(newUser->epollfd, EPOLL_CTL_ADD, clientSocket, &newEvent) < 0)
	{
		printf("add new client event to epoll instance Error: %s (errno: %d) In handleNewClientConnection\n", strerror(errno), errno);
		close(clientSocket);
		g_playing_gamer.erase(newUser->fd);
		delete newUser;
		return;
	}

	if (!InitInterface(newUser))//初始化界面
	{
		printf("Client[%d] InitInterface Error In handleNewClientConnection\n", newUser->fd);
		return;
	}
	InitBlockInfo(); //初始化方块信息
	srand((unsigned int)time(NULL)); //设置随机数生成的起点
	newUser->shape = rand() % 7;
	newUser->form = rand() % 4; //随机获取方块的形状和形态
	newUser->nextShape = rand() % 7;
	newUser->nextForm = rand() % 4;
	//随机获取下一个方块的形状和形态
	newUser->row = 1;
	newUser->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置



	if (!DrawBlock(newUser, newUser->nextShape, newUser->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
	{
		printf("Client[%d] Draw next Block Error In handleNewClientConnection\n", newUser->fd);
		return;
	}



	if (!DrawBlock(newUser, newUser->shape, newUser->form, newUser->row, newUser->col)) //将该方块显示在初始下落位置
	{
		printf("Client[%d] Draw Falling Block Error In handleNewClientConnection\n", newUser->fd);
		return;
	}

}

// 定义处理用户逻辑的函数
void processUserLogic(UserInfo* user)
{
	if (IsLegal(user, user->shape, user->form, user->row + 1, user->col) == 0)
	{
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				if (blockDefines[user->shape][user->form].space[i][j] == 1)
				{
					user->data[user->row + i - 1][user->col + j - 1] = 1;
					user->color[user->row + i - 1][user->col + j - 1] = user->shape;
				}
			}
		}

		user->line = 0;

		while (1)
		{
			if (Is_Increase_Score(user) == 1)
			{
				continue;
			}
			else if (Is_Increase_Score(user) == 0)
			{
				break;
			}
			else if (Is_Increase_Score(user) == -1)
			{
				return;
			}
		}

		if (!UpdateCurrentScore(user))
		{
			return;
		}

		if (!IsOver(user))//判断是否结束
		{
			user->shape = user->nextShape;
			user->form = user->nextForm;

			if (!DrawSpace(user, user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))
			{
				return;
			}

			user->nextShape = rand() % 7;
			user->nextForm = rand() % 4;

			user->row = 1;
			user->col = WINDOW_COL_COUNT / 2 - 1;

			if (!DrawBlock(user, user->nextShape, user->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
			{
				return;
			}

			if (!DrawBlock(user, user->shape, user->form, user->row, user->col))//将该方块显示在初始下落位置
			{
				return;
			}
		}

		else
		{
			if (!showover(user))
				return;
		}
	}
	else
	{

		if (!DrawSpace(user, user->shape, user->form, user->row, user->col))
		{
			return;
		}

		user->row++;

		if (!DrawBlock(user, user->shape, user->form, user->row, user->col))
		{
			return;
		}
	}
}

void processTimerEvent()
{
	if (!g_playing_gamer.empty())
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

			for (auto i = g_playing_gamer.begin(); i != g_playing_gamer.end(); )
			{
				// 执行一些条件检查
				if (i->second->status == STATUS_OVER_CONFIRMING)
				{
					auto eraseIter = i++;
					g_playing_gamer.erase(eraseIter); // 先使用后缀递增运算符，然后再删除元素
				}
				else if (i->second->status == STATUS_OVER_QUIT)
				{
					auto eraseIter = i++;
					printf("Client[%d] disconnected!\n", eraseIter->second->fd);
					close(eraseIter->second->fd);
					g_playing_gamer.erase(eraseIter);// 先使用后缀递增运算符，然后再删除元素
					delete eraseIter->second;
					epoll_ctl(eraseIter->second->epollfd, EPOLL_CTL_DEL, eraseIter->second->fd, nullptr);
				}
				else
				{
					processUserLogic(i->second);
					i++; // 移动到下一个元素
				}
			}
			// 将上次触发时间更新为当前时间
			lastTriggerTime = currentTime;
		}
	}
}

void handleClientData(UserInfo* userInfo)
{
	// 处理已连接客户端的数据接收事件
	char buffer[1024];
	int bytesRead = recv(userInfo->fd, buffer, sizeof(buffer), 0);
	if (bytesRead == -1) {
		userInfo->status = STATUS_OVER_QUIT;
		close(userInfo->fd);
		printf("Client[%d] recv Error: %s (errno: %d)\n", userInfo->fd, strerror(errno), errno);
	}
	else if (bytesRead == 0) {
		// 客户端连接已关闭
		userInfo->status = STATUS_OVER_QUIT;
		close(userInfo->fd);
	}
	else
	{
		if (userInfo->status == STATUS_PLAYING)
		{
			// 处理接收到的数据
			if (strcmp(buffer, KEY_DOWN) == 0)//下
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row + 1, userInfo->col) == 1) //判断方块向下移动一位后是否合法
				{
					//方块下落后合法才进行以下操作
					if (!DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
					{
						return;
					}

					userInfo->row++; //纵坐标自增（下一次显示方块时就相当于下落了一格了）

					if (!DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
					{
						return;
					}
				}
			}
			else if (strcmp(buffer, KEY_LEFT) == 0)//左
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col - 1) == 1) //判断方块向左移动一位后是否合法
				{
					//方块左移后合法才进行以下操作
					if (!DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
					{
						return;
					}

					userInfo->col--; //横坐标自减（下一次显示方块时就相当于左移了一格了）

					if (!DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
					{
						return;
					}
				}
			}
			else if (strcmp(buffer, KEY_RIGHT) == 0)//右
			{
				if (IsLegal(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col + 1) == 1) //判断方块向右移动一位后是否合法
				{
					//方块右移后合法才进行以下操作
					if (!DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
					{
						return;
					}
					userInfo->col++; //横坐标自增（下一次显示方块时就相当于右移了一格了）

					if (!DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
					{
						return;
					}
				}
			}
			else if (*buffer == ' ')
			{
				if (IsLegal(userInfo, userInfo->shape, (userInfo->form + 1) % 4, userInfo->row + 1, userInfo->col) == 1) //判断方块旋转后是否合法
				{
					//方块旋转后合法才进行以下操作
					if (!DrawSpace(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))//用空格覆盖当前方块所在位置
					{
						return;
					}

					userInfo->row++; //纵坐标自增（总不能原地旋转吧）
					userInfo->form = (userInfo->form + 1) % 4; //方块的形态自增（下一次显示方块时就相当于旋转了）

					if (!DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col))
					{
						return;
					}
				}
			}
		}
		else if (userInfo->status == STATUS_OVER_CONFIRMING)
		{
			if (*buffer == 'Y' || *buffer == 'y')
			{
				auto it = find_if(g_playing_gamer.begin(), g_playing_gamer.end(), [userInfo](const pair<int, UserInfo*>& element) {
					return element.second == userInfo;
					});

				if (it == g_playing_gamer.end())
				{
					userInfo->status = STATUS_PLAYING;

					g_playing_gamer.insert(make_pair(userInfo->fd, userInfo));

					clear(userInfo);

					userInfo->score = 0;

					if (!InitInterface(userInfo))//初始化界面
					{
						return;
					}
					InitBlockInfo(); //初始化方块信息
					srand((unsigned int)time(NULL)); //设置随机数生成的起点
					userInfo->shape = rand() % 7;
					userInfo->form = rand() % 4; //随机获取方块的形状和形态
					userInfo->nextShape = rand() % 7;
					userInfo->nextForm = rand() % 4;
					//随机获取下一个方块的形状和形态
					userInfo->row = 1;
					userInfo->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置


					if (!DrawBlock(userInfo, userInfo->nextShape, userInfo->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
					{
						return;
					}

					if (!DrawBlock(userInfo, userInfo->shape, userInfo->form, userInfo->row, userInfo->col)) //将该方块显示在初始下落位置
					{
						return;
					}
				}
			}
			else if (*buffer == 'n' || *buffer == 'N')
			{
				userInfo->status = STATUS_OVER_QUIT;
				close(userInfo->fd);
				printf("Client[%d] disconnected!\n", userInfo->fd);
			}
			else
			{
				if (!outputText(userInfo, WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "选择错误，请再次选择"))
					return;
			}
		}
	}
}

void processEvents(int readyCount, epoll_event* events, int serverSocket, int timerfd, int epollfd)
{
	for (int i = 0; i < readyCount; ++i)
	{
		UserInfo* userInfo = (UserInfo*)(events[i].data.ptr);
		int currentFd = events[i].data.fd;
		if (currentFd == serverSocket)
		{
			handleNewClientConnection(serverSocket, epollfd);
		}
		else if (currentFd == timerfd)
		{
			processTimerEvent();
		}
		else
		{
			handleClientData(userInfo);
		}
	}
}

int main()
{
	signal(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		printf("create socket Error: %s (errno: %d)\n", strerror(errno), errno);
		return -1;
	}
	// 对应伪代码中的bind(sockfd, ip::port和一些配置);
	struct sockaddr_in addr;    // 用于存放ip和端口的结构
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(DEFAULT_PORT);
	if (-1 == bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)))
	{
		close(serverSocket);
		printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
		return -1;
	}
	// 对应伪代码中的listen(sockfd);    
	if (-1 == listen(serverSocket, MAXLINK))
	{
		close(serverSocket);
		printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
		return -1;
	}

	int epollfd = epoll_create(MAXSIZE);
	if (epollfd < 0)	//创建epoll实例
	{
		close(serverSocket);
		printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
		exit(-1);
	}

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = serverSocket;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &event) == -1)
	{
		printf("add server socket to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
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
		printf("add timerfd to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
		close(serverSocket);
		close(epollfd);
		return 1;
	}


	printf("======waiting for client's request======\n");


	while (1)
	{
		struct epoll_event events[MAXSIZE];
		int readyCount = epoll_wait(epollfd, events, MAXSIZE, -1);
		if (readyCount == -1) {
			printf("Failed on epoll_wait: %s (errno: %d)\n", strerror(errno), errno);
			continue;
		}
		processEvents(readyCount, events, serverSocket, timerfd, epollfd);
	}
	return 0;
}