#include"UserInfo.h"
#include"Tetris.h"
#include"Player.h"

extern vector<Player*> players;

extern map<int, UserInfo*> g_playing_gamer;

extern Block blockDefines[7][4];//用于存储7种基本形状方块的各自的4种形态的信息，共28种

UserInfo::UserInfo(int fd) :fd(fd), line(0), score(0)
{
    this->username = "";
    this->status = STATUS_NOTSTART;
    this->password = "";
    this->Maximum_score = 0;
    this->scores = {};
    this->timestamp = "";

    this->lastTriggerTime= std::chrono::steady_clock::now();

    memset(data, 0, sizeof(data));
    memset(color, -1, sizeof(color));
}

bool UserInfo::output(string s)
{
    int bytesSent = send(fd, s.c_str(), s.length(), 0);
    if (bytesSent == -1)
    {
        if (this->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore();
        }
        status = STATUS_OVER_QUIT;
        close(this->fd);
        printf("Client[%d] send Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        return false;
    }
    else if (bytesSent == 0) 
    {
        if (this->status == STATUS_PLAYING)
        {
            Update_TopScore_RecentScore();
        }
        // 客户端连接已关闭
        status = STATUS_OVER_QUIT;
        close(this->fd);
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

    if (!moveTo(row, col))
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
    
    if (this->status == STATUS_PLAYING)
    {
        if (!moveTo(row, 0))
        {
            return false;
        }

        if (!output(string("  ")))
        {
            return false;
        }

        if (!moveTo(row, 0))
        {
            return false;
        }

        if (!ChangeCurrentColor(COLOR_WHITE))
        {
            return false;
        }

        if (!output(string("■")))
        {
            return false;
        }
    }
    

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
    
    
    if (this->status==STATUS_PLAYING)
    {
        if (!moveTo(row, 500))
        {
            return false;
        }
        
        if (!output(string(" ")))
        {
            return false;
        }
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
                if (!outputText(row + i, 2 * (col + j) - 1, Color(shape), "■"))
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
    for (i = 1; i <= WINDOW_ROW_COUNT * 10; i++)
    {
        if (!outputText(i, 1, COLOR_WHITE, emptyLine))
            return false;
    }
    return true;
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

void UserInfo::saveUserData()
{
    ofstream file("userdata.csv", std::ios::app );// 打开文件进行追加写入

    if (file.is_open() && file.good())
    { // 检查文件是否成功打开
        file << this->username << "," << this->password << endl;
        file.close(); // 关闭文件
    }
    else 
    {
        std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
    }
}

int UserInfo::ReceiveData(int epollfd) 
{
    string endMarker = "\r\n";

    char buffer[1024] = { '\0' };
    int bytesRead = recv(this->fd, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead == -1) 
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) 
        {
            // 没有可用数据，继续等待
            return 0;
        }
            close(fd);
            printf("Client[%d] recv Error: %s (errno: %d)\n", fd, strerror(errno), errno);
            return -1;
    }
    else if (bytesRead == 0)
    {
        // 客户端连接已关闭
        printf("Client[%d] disconnect!\n", this->fd);
        if (!epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr)) {
            perror("epoll_ctl EPOLL_CTL_DEL");
        }
        close(this->fd);
        return -1;
    }

    this->receivedata += buffer;

    size_t endPos = this->receivedata.find(endMarker);
    if (endPos != string::npos)
    {
        this->receivedata = this->receivedata.substr(0, endPos);
        return 1;
    }
    return 2;
}

int UserInfo::returnToInitMenu(int epollfd)
{
    int temp = this->ReceiveData(epollfd);
    if (temp==-1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata == "3")
        {
            if (!this->showInitMenu())
                return -1;
            return 1;
        }
        else
        {   
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 6, 1, COLOR_WHITE, emptyLine))
                return -1;
            if (!outputText(WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入:"))
                return -1;

            this->receivedata = "";
        }
    }
    return 2;
}

int UserInfo::returnToLoadMenu(int i,int epollfd)
{
        i++;

        int temp = this->ReceiveData(epollfd);

        if (temp == -1)
        {
            delete this;
            return -1;
        }
        else if (temp == 1)
        {
            if (this->receivedata == "3")
            {
                if (!this->showLoadMenu())
                    return -1;
                return 1;
            }
            else
            {
                this->receivedata = "";
                string emptyLine(4 * WINDOW_COL_COUNT, ' ');
                if (!outputText(WINDOW_ROW_COUNT / 2 + i, 1, COLOR_WHITE, emptyLine))
                    return -1;
                if (!outputText(WINDOW_ROW_COUNT / 2 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入:"))
                    return -1;
            }
        }

    return 2;
}

bool UserInfo::registerUser(int epollfd)
{
    if (!clear())
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入用户名："))
        return false;
    return true;
}

bool UserInfo::loadUser(int epollfd) 
{
    if (!clear())
        return false;
    
    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入用户名："))
        return false;    
    return true;
}

bool UserInfo::showInitMenu()
{
    if (!clear())
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 注册帐号"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE,"2. 登录"))
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE , "请选择操作："))
        return false;
    return true;
}

bool UserInfo::showLoadMenu()
{
    if (!clear())
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 我最近的成绩"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. 全服top成绩"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. 开始游戏"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "4. 返回服务"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "你的选择是："))
        return false;
    return true;
}

bool UserInfo::InitGameFace()
{
    if (!clear())
        return false;

    if (!InitInterface())//初始化界面
    {
        printf("Client[%d] InitInterface Error In handleNewClientConnection\n", fd);
        return false;
    }

    InitBlockInfo(); //初始化方块信息
    srand((unsigned int)time(NULL)); //设置随机数生成的起点
    this->shape = rand() % 7;
    this->form = rand() % 4; //随机获取方块的形状和形态
    this->nextShape = rand() % 7;
    this->nextForm = rand() % 4;
    //随机获取下一个方块的形状和形态
    this->row = 1;
    this->col = WINDOW_COL_COUNT / 2 - 1; //方块初始下落位置

    if (!DrawBlock(this->nextShape, this->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
    {
        return false;
    }

    if (!DrawBlock(this->shape, this->form, this->row, this->col)) //将该方块显示在初始下落位置
    {
        return false;
    }
    return true;
}

bool UserInfo::showRecentScores(int epollfd)
{
    int i = 0;

    if (!clear())
        return false;

    ifstream file("userdata.csv");

    if (!file.is_open())
    {
        std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        return false;
    }

    string line;
    getline(file, line); // 跳过第一行

    while (getline(file, line))
    {
        istringstream ss(line);

        string cell;

        getline(ss, cell, ',');//跳过用户名

        if (cell == this->username)
        {
            getline(ss, cell, ',');//跳过密码
            getline(ss, cell, ',');//跳过最高分
            getline(ss, cell, ',');//跳过最高分对应时间

            while (getline(ss, cell, ','))
            {
                if (cell!="")
                {
                    if (!outputText(WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, cell))
                        return false;
                    i++;
                }
            }
            file.close();
            break;
        }
        else
        {
            continue;
        }
    }

    if (!outputText(WINDOW_ROW_COUNT / 2 + 20, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单："))
        return false;
    return true;
}

bool UserInfo::showTopScores(int epollfd)
{
    if (!clear())
        return false;

    ifstream file("userdata.csv");

    vector<Player*> show;

    if (!file.is_open())
    {
        std::cerr << "Unable to open file or file opening failed! Error message: " << std::strerror(errno) << std::endl;
        return false;
    }

    string line;
    getline(file, line); // 跳过第一行

    while (getline(file, line))
    {
        istringstream ss(line);
        string cell;
        Player* gamer = new Player;
        getline(ss, gamer->playername, ',');//跳过用户名
        getline(ss, gamer->password, ',');//跳过密码
        getline(ss, cell, ',');//跳过最高分

        if (cell != "")
        {
            gamer->Maximum_score = stoi(cell);
        }
        else
        {
            continue;
        }

        getline(ss, gamer->timestamp, ',');

        show.push_back(gamer);
    }

    file.close();

    sort(show.begin(), show.end(), cmp);

    int i = 0;

    if (show.size() <= 10)
    {
        for (auto& player : show)
        {
            string buffer = player->playername + " " + to_string(player->Maximum_score) + " " + player->timestamp;

            if (!outputText(WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, buffer))
                return false;

            i++;
        }
    }
    else
    {
        for (auto& player : show)
        {
            string buffer = player->playername + " " + to_string(player->Maximum_score) + " " + player->timestamp;

            if (!outputText(WINDOW_ROW_COUNT / 3 + i, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, buffer))
                return false;

            i++;

            if (i == 10)
            {
                break;
            }
        }
    }

    if (!outputText(WINDOW_ROW_COUNT / 2 + 20, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回菜单："))
        return false;
    return true;
}

bool UserInfo::showGameDifficulty(int epollfd)
{
    if (!clear())
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "1. 简单"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 2, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "2. 普通"))
        return false;
    if (!outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "3. 困难"))
        return false;

    if (!outputText(WINDOW_ROW_COUNT / 2 + 6, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请选择操作："))
        return false;
    return true;
}

bool UserInfo::Update_TopScore_RecentScore()
{

    if (this->scores.size() < 20)
    {
        this->scores.push(this->score);
    }
    else
    {
        this->scores.push(this->score);
        this->scores.pop();
    }

    const string filename = "userdata.csv";

    ifstream file(filename);
    ofstream tempFile("temp_file.csv");

    if (!file.is_open() || !tempFile.is_open()) {
        cerr << "Error opening files." << endl;
        return false;
    }

    string line;

    getline(file, line);//跳过第一行

    tempFile << line << endl;

    while (getline(file, line)) {
        istringstream iss(line);
        string username, password, Maximum_score, timestamp, cell;
        getline(iss, username, ',');
        getline(iss, password, ',');
        getline(iss, Maximum_score, ',');
        getline(iss, timestamp, ',');

        if (username == this->username) {
            // 在找到匹配行时，添加新数据
            if (this->score > this->Maximum_score)
            {
                this->Maximum_score = this->score;

                tempFile << username << "," << password << "," << this->score << "," << log() << ",";

                queue<int> tempQueue = this->scores;
                while (!tempQueue.empty()) {
                    tempFile << tempQueue.front() << ",";
                    tempQueue.pop();
                }

                tempFile << endl;
            }
            else
            {
                tempFile << username << "," << password << "," << Maximum_score << "," << timestamp << ",";

                queue<int> tempQueue = this->scores;
                while (!tempQueue.empty()) {
                    tempFile << tempQueue.front() << ",";
                    tempQueue.pop();
                }

                tempFile << endl;
            }
        }
        else {
            tempFile << line << endl;
        }
    }

    file.close();
    tempFile.close();

    // 删除原文件
    remove(filename.c_str());

    // 重命名临时文件为原文件
    rename("temp_file.csv", filename.c_str());

    return true;
}

void UserInfo::resetUserInfo()
{
    this->line = 0;
    this->score = 0;
    this->username = "";
    this->status = STATUS_NOTSTART;
    this->password = "";
    this->Maximum_score = 0;
    this->scores = {};
    this->timestamp = "";
    memset(data, 0, sizeof(data));
    memset(color, -1, sizeof(color));
}

int UserInfo::receive_username_register(int epollfd)
{
    string username = "";

    int temp = this->ReceiveData(epollfd);

    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata!="")
        {
            username = this->receivedata;
            this->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "用户名不可为空，请重新输入用户名："))
                return -1;
        }
    }

    if (username!="")
    {
        if (isUserExists(username))
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "该用户名已被注册，请选择其他用户名:"))
                return -1;

            this->receivedata = "";
            return 0;
        }
        else
        {
            this->username = username;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
                return false;
            
            return 1;
        }
    }
   
    return 2;
}

int UserInfo::receive_password_register(int epollfd)
{
    if (!loadPlayerData())
        return false;

    string password = "";

    int temp = this->ReceiveData(epollfd);

    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata != "")
        {
            password = this->receivedata;
            this->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "密码不可为空，请重新输入密码："))
                return -1;
            this->receivedata = "";
        }
    }

    if (password != "")
    {
        this->password = password;

        saveUserData();

        if (!outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "注册成功！"))
            return -1;

        if (!outputText(WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请按3返回上级菜单："))
            return -1;

        return 1;
    }
    return 0;
}

int UserInfo::receive_username_load(int epollfd)
{
    string username = "";

    int temp = this->ReceiveData(epollfd);

    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata != "")
        {
            username = this->receivedata;
            this->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 1, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "用户名不可为空，请重新输入用户名："))
                return -1;
        }
    }

    if (username != "")
    {
        this->username = username;

        if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "请输入密码："))
            return false;

        return 1;
    }

    return 2;
}

int UserInfo::receive_password_load(int epollfd)
{
    if (!loadPlayerData())
        return false;

    string password = "";

    int temp = this->ReceiveData(epollfd);

    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata != "")
        {
            password = this->receivedata;
            this->receivedata = "";
        }
        else
        {
            string emptyLine(4 * WINDOW_COL_COUNT, ' ');
            if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 1, COLOR_WHITE, emptyLine))
                return -1;

            if (!outputText(WINDOW_ROW_COUNT / 2 + 3, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "密码不可为空，请重新输入密码："))
                return -1;
        }
    }

    if (password != "")
    {
        this->password = password;

        for (const auto& player : players)
        {
            if (player->playername == this->username && player->password == this->password)
            {
                this->Maximum_score = player->Maximum_score;

                for (const auto& score : player->scores)
                {
                    this->scores.push(score);
                }
                return 1;
            }
        }

        if (!outputText(WINDOW_ROW_COUNT / 2 + 4, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "登录失败，用户名或密码错误。"))
            return -1;

        if (!outputText(WINDOW_ROW_COUNT / 2 + 5, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "按3返回上级菜单:"))
            return -1;
        return 0;
    }
    return 2;
}

int UserInfo::loginUser(int epollfd)
{
    int temp = this->ReceiveData(epollfd);
    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata == "1")
        {
            if (!this->showRecentScores(epollfd))
            {
                return -1;
            }

            return 1;
        }
        else if (this->receivedata == "2")
        {
            if (!this->showTopScores(epollfd))
            {
                return -1;
            }

            return 2;
        }
        else if (this->receivedata == "3")
        {
            if (!this->showGameDifficulty(epollfd))
            {
                return -1;
            }
            return 3;
        }
        else if (this->receivedata == "4")
        {
            if (!this->showInitMenu())
            {
                return -1;
            }
            
            return 4;
        }
        else
        {
            if (!this->outputText(WINDOW_ROW_COUNT / 2 + 7, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
                return -1;
            this->receivedata = "";
        }
    }
    return 5;
}

int UserInfo::select_game_difficulty(int epollfd)
{
    int temp = this->ReceiveData(epollfd);
    if (temp == -1)
    {
        delete this;
        return -1;
    }
    else if (temp == 1)
    {
        if (this->receivedata == "1")
        {
            if (!this->InitGameFace())
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(this->fd, this));

            this->speed = 1;

            return 1;
        }
        else if (this->receivedata == "2")
        {
            if (!this->InitGameFace())
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(this->fd, this));

            this->speed = 0.5;

            return 2;
        }
        else if (this->receivedata == "3")
        {
            if (!this->InitGameFace())
            {
                return -1;
            }

            g_playing_gamer.insert(make_pair(this->fd, this));

            this->speed = 0.2;

            return 3;
        }
        else
        {
            if (!this->outputText(WINDOW_ROW_COUNT / 2 + 7, 2 * (WINDOW_COL_COUNT / 3), COLOR_WHITE, "输入错误，请重新输入: "))
                return -1;
            this->receivedata = "";
        }
    }
    return 4;
}



void UserInfo::processBlockDown()
{
    if (this->IsLegal(this->shape, this->form, this->row + 1, this->col) == 0)
    {
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                if (blockDefines[this->shape][this->form].space[i][j] == 1)
                {
                    this->data[this->row + i - 1][this->col + j - 1] = 1;
                    this->color[this->row + i - 1][this->col + j - 1] = this->shape;
                }
            }
        }

        this->line = 0;

        while (1)
        {
            if (this->Is_Increase_Score() == 1)
            {
                continue;
            }
            else if (this->Is_Increase_Score() == 0)
            {
                break;
            }
            else if (this->Is_Increase_Score() == -1)
            {
                return;
            }
        }

        if (!this->UpdateCurrentScore())
        {
            return;
        }

        if (!this->IsOver())//判断是否结束
        {
            this->shape = this->nextShape;
            this->form = this->nextForm;

            if (!this->DrawSpace(this->nextShape, this->nextForm, 3, WINDOW_COL_COUNT + 3))
            {
                return;
            }

            this->nextShape = rand() % 7;
            this->nextForm = rand() % 4;

            this->row = 1;
            this->col = WINDOW_COL_COUNT / 2 - 1;

            if (!this->DrawBlock(this->nextShape, this->nextForm, 3, WINDOW_COL_COUNT + 3))//将下一个方块显示在右上角
            {
                return;
            }

            if (!this->DrawBlock(this->shape, this->form, this->row, this->col))//将该方块显示在初始下落位置
            {
                return;
            }
        }
        else
        {
            if (!this->Update_TopScore_RecentScore())
                return;

            if (!this->showover())
                return;
        }
    }
    else
    {

        if (!this->DrawSpace(this->shape, this->form, this->row, this->col))
        {
            return;
        }

        this->row++;

        if (!this->DrawBlock(this->shape, this->form, this->row, this->col))
        {
            return;
        }
    }
}

void UserInfo::handleTimedUserLogic()
{
    
    // 记录上次触发时间
    //this->lastTriggerTime = std::chrono::steady_clock::now();

    // 获取当前时间
    this->currentTime = std::chrono::steady_clock::now();

    // 计算距离上次触发经过的时间
    std::chrono::duration<double> elapsed_time = this->currentTime - this->lastTriggerTime;

    // 计算时间差
    if (elapsed_time >= std::chrono::duration<double>(this->speed))
    {
        // 执行相应的逻辑处理
        this->processBlockDown();

        // 将上次触发时间更新为当前时间
        this->lastTriggerTime = this->currentTime;
    }
}