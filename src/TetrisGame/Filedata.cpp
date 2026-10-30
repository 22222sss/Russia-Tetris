#include"Filedata.h"
#include"PasswordHash.h"
#include"../EventLoop/EventLoop.h"
#include <sys/file.h>   // flock：多进程共享 userdata.csv 时的跨进程互斥

extern shared_ptr<spdlog::logger> logger;

namespace {

const char* const kUserDataFile = "userdata.csv";
const char* const kUserDataLockFile = "userdata.csv.lock";

// 缓存对应的文件状态。多进程共享同一份 userdata.csv 时，别的进程写过文件，
// 本进程的缓存就必须失效，否则会看不到对方刚注册/更新的账号。
time_t gCacheMtimeSec = 0;
long gCacheMtimeNsec = 0;
off_t gCacheSize = -1;

void rememberFileState() {
    struct stat st;
    if (stat(kUserDataFile, &st) == 0) {
        gCacheMtimeSec = st.st_mtime;
        gCacheMtimeNsec = st.st_mtim.tv_nsec;
        gCacheSize = st.st_size;
    } else {
        gCacheSize = -1;
    }
}

void forgetFileState() {
    gCacheSize = -1;
}

bool cacheMatchesFileState() {
    if (gCacheSize < 0) {
        return false;
    }
    struct stat st;
    if (stat(kUserDataFile, &st) != 0) {
        return false;
    }
    return st.st_size == gCacheSize
        && st.st_mtime == gCacheMtimeSec
        && st.st_mtim.tv_nsec == gCacheMtimeNsec;
}

// 跨进程读写锁（flock）。同一线程内可重入：嵌套获取只加一次锁，
// 否则"外层已持锁、内层又 flock 另一个 fd"会把自己锁死。
class DataFileLock {
public:
    explicit DataFileLock(bool exclusive) {
        if (depth_ > 0) {
            ++depth_;
            return;
        }
        depth_ = 1;
        fd_ = open(kUserDataLockFile, O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) {
            logger->warn("无法创建锁文件 {}，userdata.csv 将按单进程方式访问", kUserDataLockFile);
            return;
        }
        locked_ = (flock(fd_, exclusive ? LOCK_EX : LOCK_SH) == 0);
        if (!locked_) {
            logger->warn("flock 加锁失败，userdata.csv 将按单进程方式访问");
        }
    }

    ~DataFileLock() {
        --depth_;
        if (depth_ > 0) {
            return;
        }
        if (fd_ >= 0) {
            if (locked_) {
                flock(fd_, LOCK_UN);
            }
            close(fd_);
            fd_ = -1;
            locked_ = false;
        }
    }

    DataFileLock(const DataFileLock&) = delete;
    DataFileLock& operator=(const DataFileLock&) = delete;

private:
    static thread_local int depth_;
    static thread_local int fd_;
    static thread_local bool locked_;
};

thread_local int DataFileLock::depth_ = 0;
thread_local int DataFileLock::fd_ = -1;
thread_local bool DataFileLock::locked_ = false;

// loadPlayerData 会被两类线程同时调用：事件循环线程（注册/选难度前刷新）和
// 异步文件线程（登录校验、loadPlayerDataAsync）。两边都会"清空 + 重新填充"
// PlayerInfo::players，交错执行会重复或丢玩家，所以整段串行化。
// 加锁顺序固定为"文件锁 -> 本锁"（loginAsync 先拿共享文件锁再进来），避免反向嵌套。
std::mutex gLoadMutex;

}  // namespace

// 添加静态成员定义
std::queue<std::function<void()>> Filedata::writeQueue;
std::mutex Filedata::queueMutex;
std::condition_variable Filedata::queueCondition;
std::atomic<bool> Filedata::stopAsyncThread(false);
std::thread Filedata::asyncWriteThread;

std::unordered_map<std::string, std::shared_ptr<PlayerInfo>> Filedata::playerCache;
std::mutex Filedata::cacheMutex;
std::atomic<long> Filedata::cacheHits(0);
std::atomic<long> Filedata::cacheMisses(0);

std::vector<std::thread> Filedata::hashThreads;
std::queue<std::function<void()>> Filedata::hashQueue;
std::mutex Filedata::hashMutex;
std::condition_variable Filedata::hashCond;
std::atomic<bool> Filedata::hashStop(false);
std::atomic<bool> Filedata::hashPoolReady(false);

// 密码校验线程数：默认 2。0/1 表示不开池（校验退回文件线程）。
int Filedata::hashThreadCount() {
    static const int kDefault = 2;
    const char* raw = std::getenv("TETRIS_HASH_THREADS");
    if (!raw || !*raw) {
        return kDefault;
    }
    char* end = nullptr;
    long value = std::strtol(raw, &end, 10);
    if (end == nullptr || *end != '\0' || value < 0 || value > 32) {
        logger->warn("TETRIS_HASH_THREADS={} 不合法（0~32），改用默认值 {}", raw, kDefault);
        return kDefault;
    }
    return static_cast<int>(value);
}

void Filedata::hashWorker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(hashMutex);
            hashCond.wait(lock, [] {
                return hashStop.load() || !hashQueue.empty();
            });
            if (hashStop.load() && hashQueue.empty()) {
                return;
            }
            task = hashQueue.front();
            hashQueue.pop();
        }

        if (!task) {
            continue;
        }
        try {
            task();
        }
        catch (const std::exception& e) {
            logger->error("密码校验任务执行失败: {}", e.what());
        }
    }
}

void Filedata::ensureHashPool() {
    if (hashPoolReady.load()) {
        return;
    }

    const int count = hashThreadCount();
    if (count <= 1) {
        logger->info("密码校验线程池未启用（TETRIS_HASH_THREADS={}），校验在文件线程串行执行", count);
        return;
    }

    hashStop = false;
    try {
        for (int i = 0; i < count; ++i) {
            hashThreads.emplace_back(hashWorker);
        }
    }
    catch (const std::exception& e) {
        // 线程创建失败（极少见）：把已经建好的收掉，退回"文件线程串行校验"。
        // 绝不能让带 joinable 线程的 vector 析构（std::terminate）。
        logger->error("创建密码校验线程失败，退回串行校验: {}", e.what());
        hashStop = true;
        hashCond.notify_all();
        for (auto& thread : hashThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        hashThreads.clear();
        return;
    }
    hashPoolReady = true;
    logger->info("密码校验线程池已启动：{} 线程", count);
}

void Filedata::stopHashPool() {
    if (!hashPoolReady.exchange(false)) {
        return;
    }

    hashStop = true;
    hashCond.notify_all();
    for (auto& thread : hashThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    hashThreads.clear();

    // 正常情况下 worker 退出前已把队列处理完；这里只防御性清理
    std::lock_guard<std::mutex> lock(hashMutex);
    std::queue<std::function<void()>> empty;
    hashQueue.swap(empty);
}

void Filedata::runHashTask(std::function<void()> task) {
    if (!hashPoolReady.load()) {
        task();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(hashMutex);
        hashQueue.push(task);
    }
    hashCond.notify_one();
}

// 初始化异步系统
bool Filedata::initializeAsyncSystem() {
    stopAsyncThread = false;
    asyncWriteThread = std::thread(asyncWriteWorker);

    // 预加载玩家数据到缓存
    if (!loadPlayerData()) {
        // 加载失败时先停止异步写线程，避免 joinable 线程析构触发 terminate/core dump
        stopAsyncThread = true;
        if (asyncWriteThread.joinable()) {
            asyncWriteThread.join();
        }
        return false;
    }

    logger->info("异步文件系统初始化完成");

    // 密码校验线程池：必须在 fork 之后创建（本函数在 worker 进程里执行）
    ensureHashPool();
    return true;
}

void Filedata::shutdownAsyncSystem() {
    // 先停哈希线程：它们只做 CPU 计算，可能还会往写队列投递迁移任务，
    // 必须在文件线程退出前全部 join，保证迁移任务都已入队。
    stopHashPool();

    stopAsyncThread = true;
    queueCondition.notify_all();

    if (asyncWriteThread.joinable()) {
        asyncWriteThread.join();
    }

    // 清空缓存
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        playerCache.clear();
    }

    logger->info("异步文件系统已关闭");
}

// 异步写入工作线程
void Filedata::asyncWriteWorker() {
    while (!stopAsyncThread) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait_for(lock, std::chrono::seconds(1),
                [] { return stopAsyncThread || !writeQueue.empty(); });

            if (stopAsyncThread && writeQueue.empty()) {
                break;
            }

            if (!writeQueue.empty()) {
                task = writeQueue.front();
                writeQueue.pop();
            }
        }

        if (task) {
            try {
                task();
            }
            catch (const std::exception& e) {
                logger->error("异步写入任务执行失败: {}", e.what());
            }
        }
    }
}

// 缓存管理方法
void Filedata::addToCache(const std::shared_ptr<PlayerInfo>& player) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    playerCache[player->getPlayerName()] = player;
}

std::shared_ptr<PlayerInfo> Filedata::getFromCache(const std::string& username) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = playerCache.find(username);
    if (it != playerCache.end()) {
        cacheHits++;
        return it->second;
    }
    cacheMisses++;
    return nullptr;
}

void Filedata::removeFromCache(const std::string& username) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    playerCache.erase(username);
}

// 异步保存方法
void Filedata::saveNewUserDataAsync(const shared_ptr<User>& user) {
    // 在事件循环线程上先把值拷出来：任务真正执行时这条连接可能已经断开、
    // User 对象已被放回连接池并复用（用户名/密码会被 resetUserInfo 清空）。
    const string username = user->getUsername();
    const string password = user->getPassword();

    auto task = [username, password]() {
        // 落盘只存加盐哈希，明文密码不进入 userdata.csv。
        // 先算哈希再拿文件锁：PBKDF2 要几十毫秒，不该占着跨进程锁。
        string credential;
        try {
            credential = PasswordHash::hash(password);
        }
        catch (const std::exception& e) {
            logger->error("生成密码哈希失败，账号未保存: 用户={}, 错误={}", username, e.what());
            return;
        }

        // 跨进程独占锁：多个 worker 进程同时注册会互相覆盖
        DataFileLock fileLock(true);

        ofstream file(kUserDataFile, std::ios::app);
        if (!file.is_open()) {
            logger->error("无法打开文件保存用户数据");
            return;
        }

        // 检查文件是否为空，如果是则添加标题行
        struct stat stat_buf;
        if (stat("userdata.csv", &stat_buf) == 0 && stat_buf.st_size == 0) {
            file << "Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
                << "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
                << "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores\n";
        }

        // 写入用户数据
        file << username << "," << credential << ","
            << "0" << "," << "EASY" << "," << "" << ","
            << "0" << "," << "NORMAL" << "," << "" << ","
            << "0" << "," << "DIFFICULT" << "," << "" << ",\n";

        file.close();
        forgetFileState();   // 本进程刚改过文件，缓存的"文件状态账本"作废

        // 添加到缓存
        auto player = std::make_shared<PlayerInfo>();
        player->setPlayerName(username);
        player->setPassword(credential);
        addToCache(player);
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

void Filedata::update_TopScore_RecentScoreAsync(shared_ptr<User>& user) {
    auto task = [user]() {
        // 跨进程独占锁：这条路径是"读整个文件 -> 改 -> 写回"，
        // 两个 worker 同时做会互相覆盖（丢分数）。
        DataFileLock fileLock(true);
        Update_TopScore_RecentScore(user);
        forgetFileState();
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 把某一行的 Password 字段原地替换成新值。调用方必须已持有独占数据锁。
bool Filedata::rewriteStoredCredential(const std::string& username, const std::string& credential) {
    const string filename = kUserDataFile;
    // 临时文件名带 pid：多进程各写各的临时文件，最后 rename 覆盖，
    // 不会出现"两个进程共用 temp_file.csv 互相踩"的问题。
    const string tempName = filename + ".tmp." + std::to_string(getpid());

    ifstream in(filename);
    if (!in.is_open()) {
        logger->error("迁移密码失败：无法打开 {}", filename);
        return false;
    }

    ofstream out(tempName, std::ios::trunc);
    if (!out.is_open()) {
        logger->error("迁移密码失败：无法创建临时文件 {}", tempName);
        return false;
    }

    string line;
    bool found = false;

    // 表头原样保留
    if (getline(in, line)) {
        out << line << '\n';
    }

    while (getline(in, line)) {
        // 只替换第 2 个字段，其余（成绩、时间戳、\r 等）原样不动
        size_t firstComma = line.find(',');
        size_t secondComma = (firstComma == string::npos)
            ? string::npos : line.find(',', firstComma + 1);

        if (firstComma != string::npos && secondComma != string::npos &&
            line.compare(0, firstComma, username) == 0) {
            out << line.substr(0, firstComma) << ','
                << credential << ','
                << line.substr(secondComma + 1) << '\n';
            found = true;
        }
        else {
            out << line << '\n';
        }
    }

    out.flush();
    const bool writeOk = out.good();
    out.close();
    in.close();

    if (!writeOk) {
        logger->error("迁移密码失败：写入临时文件出错 {}", tempName);
        remove(tempName.c_str());
        return false;
    }
    if (!found) {
        logger->warn("迁移密码时没找到用户 {}，保留原文件", username);
        remove(tempName.c_str());
        return false;
    }
    if (rename(tempName.c_str(), filename.c_str()) != 0) {
        logger->error("迁移密码失败：rename 出错: {}", strerror(errno));
        remove(tempName.c_str());
        return false;
    }

    forgetFileState();
    return true;
}

void Filedata::updateCredentialInMemory(const std::string& username, const std::string& credential) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = playerCache.find(username);
        if (it != playerCache.end() && it->second) {
            it->second->setPassword(credential);
        }
    }

    auto player = PlayerInfo::findPlayer(username);
    if (player) {
        player->setPassword(credential);
    }
}

// 登录只需要"某个用户的凭据"，不需要整张玩家表。
// 原实现每次登录都调 loadPlayerData()：即使缓存命中，也会清空并重新填充
// PlayerInfo::players（O(账号数) 次 shared_ptr 拷贝，还占着文件锁和 gLoadMutex）。
// 账号越多、并发登录越多，这笔开销越明显，而它对登录结果毫无用处。
bool Filedata::lookupCredential(const std::string& username, std::string& credential) {
    // 共享锁：读缓存期间别让别的进程改文件（缓存有效性判断 + 读取必须是同一时刻的）
    DataFileLock fileLock(false);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!playerCache.empty() && cacheMatchesFileState()) {
            auto it = playerCache.find(username);
            if (it == playerCache.end() || !it->second) {
                return false;   // 文件状态没变，缓存里没有 → 这个账号不存在
            }
            credential = it->second->getPassword();
            return true;
        }
    }

    // 缓存不可用（首次登录 / 别的进程改过文件）：完整加载一次再查
    if (!loadPlayerData()) {
        return false;
    }

    auto player = PlayerInfo::findPlayer(username);
    if (!player) {
        return false;
    }
    credential = player->getPassword();
    return true;
}

void Filedata::loginAsync(const std::string& username, const std::string& password,
    std::function<void(bool success)> callback) {
    // 参数全部按值捕获：任务在异步线程上跑，不能再去读 User（连接可能已断、对象已被复用）。
    auto task = [username, password, callback]() {
        // 回到事件循环线程再动 User / socket（与其它异步回调保持同一约定）
        const auto finish = [callback](bool ok) {
            EventLoop::post([callback, ok]() { callback(ok); });
            };

        string stored;
        try {
            if (!lookupCredential(username, stored)) {
                finish(false);
                return;
            }
        }
        catch (const std::exception& e) {
            logger->error("登录校验异常: 用户={}, 错误={}", username, e.what());
            finish(false);
            return;
        }

        // PBKDF2 是纯 CPU 计算：交给哈希线程池并行；池未启用时（0/1 线程）在这里同步执行。
        runHashTask([username, password, stored, finish]() {
            bool needsRehash = false;
            bool ok = false;
            try {
                ok = PasswordHash::verify(password, stored, &needsRehash);
            }
            catch (const std::exception& e) {
                logger->error("密码校验异常: 用户={}, 错误={}", username, e.what());
                finish(false);
                return;
            }

            if (!ok) {
                finish(false);
                return;
            }
            if (!needsRehash) {
                finish(true);
                return;
            }

            // 旧版明文（或参数落后的哈希）：迁移成当前格式。
            // 新哈希继续在当前线程算；落盘必须回单写线程（写队列），否则会与其它文件操作并发。
            string upgraded;
            try {
                upgraded = PasswordHash::hash(password);
            }
            catch (const std::exception& e) {
                logger->error("迁移密码时生成哈希失败，保留旧值: 用户={}, 错误={}", username, e.what());
                finish(true);   // 迁移失败不影响本次登录
                return;
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                writeQueue.push([username, upgraded, finish]() {
                    DataFileLock fileLock(true);
                    if (rewriteStoredCredential(username, upgraded)) {
                        updateCredentialInMemory(username, upgraded);
                        logger->info("账号密码已迁移为加盐哈希: {}", username);
                    }
                    else {
                        logger->warn("账号密码迁移失败，保留旧值: {}", username);
                    }
                    finish(true);
                    });
            }
            queueCondition.notify_one();
            });
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 修改loadPlayerData使用缓存
bool Filedata::loadPlayerData() {
    // 共享锁：避免读到别的进程正在写入的半行
    DataFileLock fileLock(false);
    // 本进程内串行化"清空 + 填充"，否则两个线程交错会把玩家列表弄重复/弄丢
    std::lock_guard<std::mutex> loadLock(gLoadMutex);

    PlayerInfo::clearPlayers();

    // 先尝试从缓存加载。多进程共享同一个 userdata.csv，缓存必须与文件状态
    // 对得上才能用，否则会看不到别的进程刚注册的账号。
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!playerCache.empty() && cacheMatchesFileState()) {
            for (const auto& pair : playerCache) {
                PlayerInfo::addPlayer(pair.second);
            }
            logger->info("从缓存加载了 {} 个玩家数据", playerCache.size());
            return true;
        }
        playerCache.clear();   // 文件已被别的进程改过，旧缓存作废
    }

    // 缓存不可用，从文件加载
    ifstream file("userdata.csv");
    if (!file.is_open()) {
        // 首次运行：数据文件不存在时自动创建带表头的空文件（不含任何用户数据），
        // 保证 clone 仓库后无需手工准备数据文件即可启动
        ofstream create("userdata.csv");
        if (!create.is_open()) {
            logger->error("无法创建用户数据文件");
            return false;
        }
        create << "Username,Password,Easy_Score,Easy_Difficulty,Easy_Timestamp,"
               << "Normal_Score,Normal_Difficulty,Normal_Timestamp,"
               << "Difficult_Score,Difficult_Difficulty,Difficult_Timestamp,Recent_Scores\n";
        create.close();
        rememberFileState();
        logger->info("用户数据文件不存在，已自动创建空数据文件");
        return true;
    }

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line)) {
        if (line.empty()) continue;

        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        if (line.empty()) continue;

        istringstream ss(line);
        string cell;

        auto player = std::make_shared<PlayerInfo>(); // 使用智能指针
        string playerName = "";
        string password = "";

        getline(ss, playerName, ',');
        getline(ss, password, ',');

        player->setPlayerName(playerName);
        player->setPassword(password);

        // 读取EASY难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Easy(0);
        }

        // 跳过EASY难度字符串
        getline(ss, cell, ',');

        // 读取EASY难度时间戳
        string timestampEasy;
        getline(ss, timestampEasy, ',');
        player->setTimestampEasy(timestampEasy);

        // 读取NORMAL难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Normal(0);
        }

        // 跳过NORMAL难度字符串
        getline(ss, cell, ',');

        // 读取NORMAL难度时间戳
        string timestampNormal;
        getline(ss, timestampNormal, ',');
        player->setTimestampNormal(timestampNormal);

        // 读取DIFFICULT难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            player->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            player->setMaximum_Score_Difficult(0);
        }

        // 跳过DIFFICULT难度字符串
        getline(ss, cell, ',');

        // 读取DIFFICULT难度时间戳
        string timestampDifficult;
        getline(ss, timestampDifficult, ',');
        player->setTimestampDifficult(timestampDifficult);

        // 读取最近的成绩记录
        while (getline(ss, cell, ','))
        {
            if (!cell.empty() && isNumber(cell))
            {
                int score = stoi(cell);
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        player->setScores_Push(score);
                        player->setGameDiffclutys_Push(difficulty);
                    }
                }
            }
        }

        // 添加到缓存
        addToCache(player);
        PlayerInfo::addPlayer(player);
    }

    file.close();
    rememberFileState();   // 记下这份缓存对应的文件状态
    logger->info("从文件加载了 {} 个玩家数据到缓存", playerCache.size());
    return true;
}

// 缓存统计
void Filedata::printCacheStatistics() {
    logger->info("缓存统计 - 命中: {}, 未命中: {}, 缓存大小: {}",
        cacheHits.load(), cacheMisses.load(), playerCache.size());
}

vector<string> Filedata::Read_recent_grades(const shared_ptr<User>& user)
{
    vector<string> temp = {};
    ifstream file("userdata.csv");

    if (!file.is_open())
    {
        logger->error("Unable to open file or file opening failed! Error message: {}\n", strerror(errno));
        logger->flush();
        temp.push_back("-1");
        return temp;
    }

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line))
    {
        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;

        getline(ss, cell, ',');//读取用户名

        if (cell == user->getUsername())
        {
            getline(ss, cell, ',');//读取密码

            // 跳过EASY难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 跳过NORMAL难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 跳过DIFFICULT难度数据
            getline(ss, cell, ',');
            getline(ss, cell, ',');
            getline(ss, cell, ',');

            // 读取最近的成绩
            int count = 0;
            while (getline(ss, cell, ','))
            {
                if (cell.empty()) continue;

                string score = cell;
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        string outcome = score + "  " + difficulty;
                        temp.push_back(outcome);
                        count++;

                        // 只保留最近的20条记录
                        if (count >= 20) break;
                    }
                }
            }
            file.close();

            if (temp.empty())
            {
                temp.push_back("1"); // 表示用户存在但没有成绩记录
            }

            return temp;
        }
    }

    file.close();
    return temp;
}

vector<shared_ptr<PlayerInfo>> Filedata::Read_AllpalyerInfo(ifstream& file)
{
    vector<shared_ptr<PlayerInfo>> show = {};

    string line;
    getline(file, line); // 读取标题行

    while (getline(file, line))
    {
        // 移除Windows换行符\r
        if (!line.empty() && line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        istringstream ss(line);
        string cell;
        std::shared_ptr<PlayerInfo> gamer = std::make_shared<PlayerInfo>();

        string playerName = "";
        string Password = "";

        getline(ss, playerName, ',');//读取用户名
        gamer->setPlayerName(playerName);

        getline(ss, Password, ',');//读取密码
        gamer->setPassword(Password);

        // 读取EASY难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Easy(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Easy(0);
        }

        // 跳过EASY难度字符串
        getline(ss, cell, ',');

        // 读取EASY难度时间戳
        string TimestampEasy;
        getline(ss, TimestampEasy, ',');
        gamer->setTimestampEasy(TimestampEasy);

        // 读取NORMAL难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Normal(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Normal(0);
        }

        // 跳过NORMAL难度字符串
        getline(ss, cell, ',');

        // 读取NORMAL难度时间戳
        string TimestampNormal;
        getline(ss, TimestampNormal, ',');
        gamer->setTimestampNormal(TimestampNormal);

        // 读取DIFFICULT难度最高分
        getline(ss, cell, ',');
        if (!cell.empty() && isNumber(cell))
        {
            gamer->setMaximum_Score_Difficult(stoi(cell));
        }
        else
        {
            gamer->setMaximum_Score_Difficult(0);
        }

        // 跳过DIFFICULT难度字符串
        getline(ss, cell, ',');

        // 读取DIFFICULT难度时间戳
        string TimestampDifficult;
        getline(ss, TimestampDifficult, ',');
        gamer->setTimestampDifficult(TimestampDifficult);

        // 读取最近的成绩记录
        while (getline(ss, cell, ','))
        {
            if (!cell.empty() && isNumber(cell))
            {
                int score = stoi(cell);
                string difficulty;

                if (getline(ss, difficulty, ','))
                {
                    if (!difficulty.empty())
                    {
                        gamer->setScores_Push(score);
                        gamer->setGameDiffclutys_Push(difficulty);
                    }
                }
            }
        }

        show.push_back(gamer);
    }


    file.close();
    return show;
}

bool Filedata::Update_TopScore_RecentScore(const shared_ptr<User>& user)
{
    // 添加空指针检查
    if (user == nullptr) {
        logger->error("User pointer is null in Update_TopScore_RecentScore");
        logger->flush();
        return false;
    }

    // 添加对用户分数队列的检查
    if (user->getScores().size() < 20)
    {
        user->setScores_Push(user->getScore());
        user->setGameDiffclutys_Push(user->getGameDiffculty());
    }
    else
    {
        user->setScores_Push(user->getScore());
        user->setScores_Pop();

        user->setGameDiffclutys_Push(user->getGameDiffculty());
        user->setGameDiffclutys_Pop();
    }

    const string filename = "userdata.csv";

    // 添加文件存在性检查
    if (access(filename.c_str(), F_OK) == -1) {
        logger->error("File does not exist: {}", filename);
        logger->flush();
        return false;
    }

    ifstream file(filename);
    ofstream tempFile("temp_file.csv");

    if (!file.is_open() || !tempFile.is_open()) {
        logger->error("Error opening files.");
        logger->flush();
        return false;
    }

    string line;

    // 跳过第一行
    if (!getline(file, line)) {
        logger->error("Failed to read header from file.");
        logger->flush();
        file.close();
        tempFile.close();
        return false;
    }

    tempFile << line << endl;

    while (getline(file, line))
    {
        istringstream iss(line);
        string username, password, Maximum_score_easy, timestamp_easy, Maximum_score_normal, timestamp_normal, Maximum_score_diffcult, timestamp_diffcult, cell;

        // 添加解析检查
        if (!getline(iss, username, ',') ||
            !getline(iss, password, ',') ||
            !getline(iss, Maximum_score_easy, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_easy, ',') ||
            !getline(iss, Maximum_score_normal, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_normal, ',') ||
            !getline(iss, Maximum_score_diffcult, ',') ||
            !getline(iss, cell, ',') ||
            !getline(iss, timestamp_diffcult, ',')) {

            logger->warn("Skipping malformed line: {}", line);
            continue;
        }

        if (username == user->getUsername())
        {
            if (user->getGameDiffculty() == "EASY")
            {
                if (user->getScore() > user->getMaximum_Score_Easy())
                {
                    user->setMaximum_Score_Easy(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << user->getScore() << "," << "EASY" << "," << currenttime() << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
            else if (user->getGameDiffculty() == "NORMAL")
            {
                if (user->getScore() > user->getMaximum_Score_Normal())
                {
                    user->setMaximum_Score_Normal(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << user->getScore() << "," << "NORMAL" << "," << currenttime() << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
            else if (user->getGameDiffculty() == "DIFFICULT")
            {
                if (user->getScore() > user->getMaximum_Score_Diffcult())
                {
                    user->setMaximum_Score_Diffcult(user->getScore());

                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << user->getScore() << "," << "DIFFICULT" << "," << currenttime() << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
                else
                {
                    tempFile << username << "," << password << ",";

                    tempFile << Maximum_score_easy << "," << "EASY" << "," << timestamp_easy << ",";

                    tempFile << Maximum_score_normal << "," << "NORMAL" << "," << timestamp_normal << ",";

                    tempFile << Maximum_score_diffcult << "," << "DIFFICULT" << "," << timestamp_diffcult << ",";

                    // 添加队列空检查
                    queue<int> tempQueue = user->getScores();
                    queue<string> tempQueue1 = user->getGameDiffclutys();
                    if (tempQueue.size() != tempQueue1.size()) {
                        logger->warn("Queue size mismatch for user: {}", username);
                    }

                    while (!tempQueue.empty() && !tempQueue1.empty())
                    {
                        tempFile << tempQueue.front() << "," << tempQueue1.front() << ",";
                        tempQueue.pop();
                        tempQueue1.pop();
                    }

                    tempFile << endl;
                }
            }
        }
        else
        {
            tempFile << line << endl;
        }
    }

    file.close();
    tempFile.close();

    // 删除原文件前检查临时文件是否成功创建
    ifstream tempCheck("temp_file.csv");
    if (!tempCheck.is_open()) {
        logger->error("Temporary file was not created successfully.");
        logger->flush();
        return false;
    }
    tempCheck.close();

    // 删除原文件
    if (remove(filename.c_str()) != 0) {
        logger->error("Failed to delete original file: {}", filename);
        logger->flush();
        return false;
    }

    // 重命名临时文件为原文件
    if (rename("temp_file.csv", filename.c_str()) != 0) {
        logger->error("Failed to rename temporary file.");
        logger->flush();
        return false;
    }

    return true;
}

// 🔥 新增：异步读取最近成绩（带callback）
void Filedata::readRecentGradesAsync(const shared_ptr<User>& user,std::function<void(bool success, vector<string> result)> callback) 
{
    auto task = [user, callback]() {
        // 共享锁：和写入方互斥，避免读到写了一半的文件
        DataFileLock fileLock(false);
        try {
            vector<string> result = Read_recent_grades(user);
            bool success = !(result.empty() || result.front() == "-1");

            // 通过callback返回结果。
            // 关键：回调里会操作 User 和 socket，必须回到事件循环线程执行，
            // 否则会与事件循环线程竞争 receivedata / status 等字段。
            EventLoop::post([callback, success, result]() { callback(success, result); });

            logger->debug("异步读取最近成绩完成: 用户={}, 记录数={}",
                user->getUsername(), result.size());
        }
        catch (const std::exception& e) {
            logger->error("异步读取最近成绩失败: 用户={}, 错误={}",
                user->getUsername(), e.what());
            EventLoop::post([callback]() { callback(false, {}); });
        }
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 🔥 新增：异步读取所有玩家信息（带callback）
void Filedata::readAllPlayerInfoAsync(std::function<void(bool success, vector<shared_ptr<PlayerInfo>> result)> callback) {
    auto task = [callback]() {
        // 共享锁：和写入方互斥，避免读到写了一半的文件
        DataFileLock fileLock(false);
        try {
            ifstream file("userdata.csv");
            if (!file.is_open()) {
                logger->error("无法打开用户数据文件");
                EventLoop::post([callback]() { callback(false, {}); });
                return;
            }

            vector<shared_ptr<PlayerInfo>> result = Read_AllpalyerInfo(file);
            EventLoop::post([callback, result]() { callback(true, result); });

            logger->debug("异步读取所有玩家信息完成: 玩家数={}", result.size());
        }
        catch (const std::exception& e) {
            logger->error("异步读取所有玩家信息失败: {}", e.what());
            EventLoop::post([callback]() { callback(false, {}); });
        }
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 🔥 新增：异步加载玩家数据（带callback）
void Filedata::loadPlayerDataAsync(std::function<void(bool success)> callback) {
    auto task = [callback]() {
        try {
            bool success = loadPlayerData();
            EventLoop::post([callback, success]() { callback(success); });

            if (success) {
                logger->debug("异步加载玩家数据完成");
            }
            else {
                logger->error("异步加载玩家数据失败");
            }
        }
        catch (const std::exception& e) {
            logger->error("异步加载玩家数据异常: {}", e.what());
            EventLoop::post([callback]() { callback(false); });
        }
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}
