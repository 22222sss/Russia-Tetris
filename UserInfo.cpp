#include"UserInfo.h"
#include"Tetris.h"

map<int, UserInfo*> g_playing_gamer = {};

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

UserInfo::UserInfo(int fd,int status,int epollfd):fd(fd),line(0),score(0),status(status),epollfd(epollfd)
{
	memset(data, 0, sizeof(data));
	memset(color, -1, sizeof(color));
}


bool UserInfo::output(string s)
{
    int bytesSent = send(fd, s.c_str(), s.length(), 0);
    if (bytesSent == -1)
    {
        status = STATUS_OVER_QUIT;
        printf("Client[%d] send Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        return false;
    }
    else if (bytesSent == 0) {
        // 客户端连接已关闭
        status = STATUS_OVER_QUIT;
        return false;
    }
    return true;
}

bool UserInfo::moveTo(int row, int col) {
    // 检查输入是否合法
    if (row < 0 || col < 0) {
        cerr << "Invalid row or col number" << endl;
        // 可以根据具体情况进行相应的错误处理操作
        return false;
    }

    string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
    if (!output(command))
        return false;
    return true;
}

bool UserInfo::ChangeCurrentColor(int n) {
    // 检查输入是否合法
    if (n < 0 || n > 255) {
        cerr << "Invalid color number" << endl;
        // 可以根据具体情况进行相应的错误处理操作
        return false;
    }

    string command = "\33[" + to_string(n) + "m";
    if (!output(command))
        return false;
    return true;
}

bool UserInfo::outputgrade(int row, int col, int n, string s, int grade)
{
    // 检查输入是否合法
    if (grade < 0) {
        cerr << "Invalid grades: " << grade << endl;
        // 可以根据具体情况进行相应的错误处理操作
        return false;
    }

    if (!moveTo( row, col))
    {
        return false;
    }
    if (!ChangeCurrentColor(n))
    {
        return false;
    }

    string command = s + to_string(grade);

    if (!output(command))
    {
        return false;
    }
    return true;
}

bool UserInfo::outputText(int row, int col, int n, string s)
{
    if (!moveTo(row, col))
    {
        return false;
    }
    if (!ChangeCurrentColor(n))
    {
        return false;
    }
    if (!output(s))
    {
        return false;
    }
    return true;
}

bool UserInfo::InitInterface()
{
    for (int i = 0; i < WINDOW_ROW_COUNT; i++)
    {
        for (int j = 0; j < WINDOW_COL_COUNT + 10; j++)
        {
            if (j == 0 || j == WINDOW_COL_COUNT - 1 || j == WINDOW_COL_COUNT + 9)
            {
                data[i][j] = 1; //标记该位置有方块

                if (!outputText(i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else if (i == WINDOW_ROW_COUNT - 1)
            {
                data[i][j] = 1; //标记该位置有方块

                if (!outputText(i + 1, 2 * (j + 1) - 1, COLOR_WHITE, "■"))
                    return false;
            }
            else
                data[i][j] = 0; //标记该位置无方块
        }
    }

    for (int i = WINDOW_COL_COUNT; i < WINDOW_COL_COUNT + 10; i++)
    {
        data[11][i] = 1; //标记该位置有方块
        if (!outputText(11 + 1, 2 * i + 1, COLOR_WHITE, "■"))
            return false;
    }

    if (!outputText(2, 2 * WINDOW_COL_COUNT + 1 + 1, COLOR_WHITE, "Next:"))
        return false;


    if (!outputText(14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: "))
        return false;

    return true;
}

bool UserInfo::DrawBlock(int shape, int form, int row, int col)//row和col，指的是方块信息当中第一行第一列的方块的打印位置为第row行第col列
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText( row + i, 2 * (col + j) - 1, Color(shape), "■"))
                    return false;
            }
        }
    }
    return true;
}

bool UserInfo::DrawSpace(int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (blockDefines[shape][form].space[i][j] == 1)//如果该位置有方块
            {
                if (!outputText(row + i, 2 * (col + j) - 1, COLOR_WHITE, "  "))
                    return false;
            }
        }
    }
    return true;
}

bool UserInfo::IsLegal(int shape, int form, int row, int col)
{
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if ((blockDefines[shape][form].space[i][j] == 1) && (data[row + i - 1][col + j - 1] == 1))
                return false;
        }
    }
    return true;
}

int UserInfo::Is_Increase_Score()
{
    int i = 0, j = 0;
    //判断是否得分
    for (i = WINDOW_ROW_COUNT - 2; i > 4; i--)
    {
        int sum = 0;
        for (j = 1; j < WINDOW_COL_COUNT - 1; j++)
        {
            sum += data[i][j];
        }
        if (sum == 0)
            break;
        if (sum == WINDOW_COL_COUNT - 2)//该行全是方块，可得分
        {
            line++;
            for (j = 1; j < WINDOW_COL_COUNT - 1; j++)//清除得分行的方块信息
            {
                data[i][j] = 0;
                if (!outputText(i + 1, 2 * j + 1, COLOR_WHITE, "  "))
                    return -1;

            }
            //把被清除行上面的行整体向下挪一格
            for (int m = i; m > 1; m--)
            {
                sum = 0;//记录上一行的方块个数
                for (int n = 1; n < WINDOW_COL_COUNT - 1; n++)
                {
                    sum += data[m - 1][n];//统计上一行的方块个数
                    data[m][n] = data[m - 1][n];//将上一行方块的标识移到下一行
                    color[m][n] = color[m - 1][n];//将上一行方块的颜色编号移到下一行
                    if (data[m][n] == 1)
                    {
                        if (!outputText(m + 1, 2 * n + 1, Color(color[m][n]), "■"))
                            return -1;
                    }
                    else
                    {
                        if (!outputText(m + 1, 2 * n + 1, COLOR_WHITE, "  "))
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

bool UserInfo::UpdateCurrentScore()
{
    if (line >= 2)
    {
        score += (line + 1) * 10;
        if (!outputgrade(14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", score))
        {
            return false;
        }
    }
    else
    {
        score += line * 10;
        if (!outputgrade(14, 2 * WINDOW_COL_COUNT + 2, COLOR_WHITE, "Score: ", score))
        {
            return false;
        }
    }
    return true;
}

bool UserInfo::clear()
{
    int i;
    string emptyLine(4 * WINDOW_COL_COUNT, ' ');
    for (i = 1; i <= WINDOW_ROW_COUNT; i++)
    {
        if (!outputText(i, 1, COLOR_WHITE, emptyLine))
            return false;
    }
    return false;
}

bool UserInfo::IsOver()
{
    //判断游戏是否结束
    for (int j = 1; j < WINDOW_COL_COUNT - 1; j++)
    {
        if (data[1][j] == 1) //顶层有方块存在（以第1行为顶层，不是第0行）
        {
            status = STATUS_OVER_CONFIRMING;
            return true;
        }
    }
    return false;
}

bool UserInfo::showover()
{
    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "GAME OVER"))
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "Start Again ? (y/n):"))
        return false;

    return true;
}

