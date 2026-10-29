#include"Utility.h"

extern shared_ptr<spdlog::logger> logger;

bool output(const shared_ptr<User>& user, string s)
{
    // 直接发送 UTF-8 文本（现代终端均支持 UTF-8，无需每次 output 做 GBK 转换）
    //
    // socket 是非阻塞的：send 可能只写出去一部分，甚至返回 EAGAIN。
    // 旧实现只看 -1/0，把"部分写"和 EAGAIN 都当成功，等于静默丢字节/丢整条消息。
    // 现在统一进发送缓冲，由 flushUserOutput 写多少算多少。
    {
        std::lock_guard<std::mutex> lock(user->getSendMutex());
        user->getSendBuffer() += s;
        user->setPendingOutput(true);
    }
    return flushUserOutput(user);
}

bool flushUserOutput(const shared_ptr<User>& user)
{
    std::lock_guard<std::mutex> lock(user->getSendMutex());

    string& buffer = user->getSendBuffer();
    if (buffer.empty())
    {
        user->setPendingOutput(false);
        return true;
    }

    // 兜底：客户端长时间不读时缓冲区会一直涨，超过上限按死连接处理，
    // 避免被一个卡住的客户端拖爆内存。
    static const size_t MAX_SEND_BUFFER = 1u << 20;  // 1 MB
    if (buffer.size() > MAX_SEND_BUFFER)
    {
        buffer.clear();
        user->setPendingOutput(false);
        user->setStatus(STATUS_OVER_QUIT);
        logger->error("Client[{}] 待发数据超过 {} 字节，判定为不读数据的死连接",
                      user->getFd(), MAX_SEND_BUFFER);
        logger->flush();
        close(user->getFd());
        return false;
    }

    size_t sent = 0;
    while (sent < buffer.size())
    {
        ssize_t n = send(user->getFd(), buffer.data() + sent, buffer.size() - sent, 0);
        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == -1 && errno == EINTR)
        {
            continue;   // 被信号打断，重试
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;      // 内核发送缓冲已满：剩下的留着下次发，不能丢
        }
        if (n == 0)
        {
            break;      // 本次一个字节都没写出去，留待下次
        }

        // 真正的错误：连接已不可用
        buffer.clear();
        user->setPendingOutput(false);
        user->setStatus(STATUS_OVER_QUIT);
        logger->error("Client[{}] send Error: {} (errno: {})", user->getFd(), strerror(errno), errno);
        logger->flush();
        close(user->getFd());
        return false;
    }

    if (sent > 0)
    {
        buffer.erase(0, sent);
    }
    user->setPendingOutput(!buffer.empty());
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