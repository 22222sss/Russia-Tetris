#include"Utility.h"

extern shared_ptr<spdlog::logger> logger;

string utf8_to_gbk(const std::string& utf8_str) {
    if (utf8_str.empty()) return "";

    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == (iconv_t)-1) {
        return utf8_str; // 转换失败，返回原字符串
    }

    size_t in_len = utf8_str.length();
    size_t out_len = in_len * 2; // GBK 可能占用更多字节
    char* in_buf = const_cast<char*>(utf8_str.c_str());
    char* out_buf = new char[out_len + 1];
    char* out_ptr = out_buf;

    memset(out_buf, 0, out_len + 1);

    if (iconv(cd, &in_buf, &in_len, &out_ptr, &out_len) == (size_t)-1) {
        delete[] out_buf;
        iconv_close(cd);
        return utf8_str; // 转换失败
    }

    std::string result(out_buf);
    delete[] out_buf;
    iconv_close(cd);
    return result;
}

bool output(const shared_ptr<User>& user, string s)
{
    // 🔥 新增：检测如果是 Windows telnet 客户端，转换为 GBK
    static std::unordered_map<int, bool> encoding_detected;
    static std::unordered_map<int, bool> is_windows_client;

    // 第一次输出时检测客户端类型
    if (!encoding_detected[user->getFd()]) {
        // 简单检测：Windows telnet 通常不会发送 UTF-8 设置
        // 我们可以假设所有客户端都是 Windows telnet，或者添加更复杂的检测
        is_windows_client[user->getFd()] = true; // 假设是 Windows
        encoding_detected[user->getFd()] = true;
    }

    // 如果是 Windows 客户端，转换为 GBK
    if (is_windows_client[user->getFd()]) {
        s = utf8_to_gbk(s);
    }
     

    int bytesSent = send(user->getFd(), s.c_str(), s.length(), 0);
    if (bytesSent == -1)
    {
        // 非阻塞 socket 发送缓冲区满时返回 EAGAIN/EWOULDBLOCK，
        // 这是正常情况，不应关闭连接，稍后由事件循环继续处理
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return true;
        }

        user->setStatus(STATUS_OVER_QUIT);

        close(user->getFd());
        //printf("Client[%d] send Error: %s (errno: %d)\n", fd, strerror(errno), errno);
        logger->error("Client[{}] send Error: {} (errno: {})\n", user->getFd(), strerror(errno), errno);
        logger->flush();
        return false;
    }
    else if (bytesSent == 0)
    {
        // 客户端连接已关闭
        user->setStatus(STATUS_OVER_QUIT);

        close(user->getFd());
        return false;
    }
    return true;
}

bool moveTo(const shared_ptr<User>& user, int row, int col) {
    // 检查输入是否合法
    if (row < 0 || col < 0) {
        //cerr << "Invalid row or col number" << endl;

        // 可以根据具体情况进行相应的错误处理操作
        logger->error("Invalid row or col number\n");
        logger->flush();
        return false;

    }

    string command = "\x1b[" + to_string(row) + ";" + to_string(col) + "H";
    if (!output(user, command))
        return false;
    return true;
}

bool ChangeCurrentColor(const shared_ptr<User>& user, int n)
{
    // 检查输入是否合法
    if (n < 0 || n > 255) {
        //cerr << "Invalid color number" << endl;
        // 可以根据具体情况进行相应的错误处理操作
        logger->error("Invalid row or col number\n");
        logger->flush();
        return false;
    }

    string command = "\33[" + to_string(n) + "m";
    if (!output(user, command))
        return false;
    return true;
}

bool outputText(const shared_ptr<User>& user, int row, int col, int color, const string& text, int grade) //有分数则输出分数
{
    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!moveTo(user, row, 0))
        {
            return false;
        }

        if (!output(user, string("  ")))
        {
            return false;
        }

        if (!moveTo(user, row, 0))
        {
            return false;
        }

        if (!ChangeCurrentColor(user, COLOR_WHITE))
        {
            return false;
        }

        if (!output(user, string("■")))
        {
            return false;
        }
    }


    // 检查输入是否合法
    // 参数验证 - 只有当明确传入了grade时才验证
    if (grade < 0) 
    {
        logger->error("Invalid grades: {}", grade);
        logger->flush();
        return false;
    }



    // 构建输出内容
    string outputContent = text;

    if (grade > 0)
    {
        outputContent += to_string(grade);
    }

    // 合并定位、颜色、内容为一次 send，减少系统调用次数
    string cmd = "\x1b[" + to_string(row) + ";" + to_string(col) + "H"
               + "\33[" + to_string(color) + "m" + outputContent;
    if (!output(user, cmd))
    {
        return false;
    }

    if (user->getStatus() == STATUS_PLAYING)
    {
        if (!moveTo(user, row, 500))
        {
            return false;
        }

        if (!output(user, string(" ")))
        {
            return false;
        }
    }

    return true;
}

bool IsSetSocketBlocking(int socket, bool blocking) {
    // 获取套接字标志
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        close(socket);
        //std::cerr << "Failed to get socket flags" << std::endl;
        logger->error("Failed to get socket flags");
        logger->flush();
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
        //std::cerr << "Failed to set socket mode" << std::endl;
        logger->error("Failed to get socket flags");
        logger->flush();
        return false;
    }
    return true;
}

int Color(int c)
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

bool isUserExists(const string& playername)
{

    for (auto& player : PlayerInfo::getPlayers())
    {
        if (player->getPlayerName() == playername)
        {
            return true;
        }
    }
    return false;
}

string currenttime()//日志函数
{
    char timestamp[20];
    time_t now = time(nullptr);

    struct tm* timeinfo = localtime(&now);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    return timestamp;
}

// 检查字符串是否为数字
bool isNumber(const std::string& s) 
{
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}