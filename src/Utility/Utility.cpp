#include"Utility.h"
#include"../TetrisGame/Db.h"

extern shared_ptr<spdlog::logger> logger;

namespace {
// >0 表示当前在 OutputBatch 里：output() 只写缓冲，由最外层析构统一 flush。
// 事件循环是单线程的，用 thread_local 即可（异步回调也已经 post 回循环线程）。
thread_local int gOutputBatchDepth = 0;
}

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

    // 批量模式：先攒着，等 OutputBatch 析构时一次 send 发出去
    if (gOutputBatchDepth > 0) {
        return true;
    }
    return flushUserOutput(user);
}

OutputBatch::OutputBatch(const shared_ptr<User>& user) : user_(user) {
    ++gOutputBatchDepth;
}

OutputBatch::~OutputBatch() {
    --gOutputBatchDepth;
    if (gOutputBatchDepth > 0) {
        return;   // 嵌套的内层不发送，交给最外层
    }
    // 连接已经出错关闭时不再 send：fd 可能已被 close，重复 close 会误伤复用的 fd
    if (user_ && user_->getFd() >= 0 && user_->getStatus() != STATUS_OVER_QUIT) {
        flushUserOutput(user_);
    }
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
    // 这里曾经有一段"游戏进行中先在 (row, 0) 画个白方块"的逻辑：那是早期留下的
    // 调试痕迹，列号 0 在 ANSI 里不合法（从 1 开始计），终端一律钳到第 1 列，
    // 于是方块每下落一格、每渲染一个格子，就在最左边糊一个白块（2026-11-05 反馈）。
    // 而且它让每个格子的渲染多出 4 次 send，去掉顺带省了这部分输出。

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
        // 这里原来还会 output(" ")：把光标挪到第 500 列（会被终端钳到最后一列）再写一个
        // 空格。写到最后一列会让终端进入"待折行"状态，在最底下一行还会把整屏向上滚一行 ——
        // 方块每落/锁一次都在底部写一格，画面就持续上移，边框错位、左边缘出现残影
        // （2026-11-06 定位到的真凶）。移动光标本身不触发换行，所以只保留移动。
        if (!moveTo(user, row, 500))
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

// 注册前查重：直接问 SQLite，不再依赖"整张玩家表已经在内存里"这个隐含前提
bool isUserExists(const string& playername)
{
    if (playername.empty())
    {
        return false;
    }
    return Db::accountExists(playername);
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