#include"Filedata.h"
#include"PasswordHash.h"
#include"Db.h"
#include"../EventLoop/EventLoop.h"

extern shared_ptr<spdlog::logger> logger;

namespace {

// PlayerInfo 快照：由 loadPlayerData() 从 SQLite 重建。
// 用 Db::cacheStamp()（本进程写次数 + SQLite data_version）判断要不要重建，
// 替代原来"按文件 mtime/size 失效"的那套账本。
std::mutex gLoadMutex;
std::uint64_t gPlayersStamp = 0;
bool gPlayersLoaded = false;

}  // namespace

// 添加静态成员定义
std::queue<std::function<void()>> Filedata::writeQueue;
std::mutex Filedata::queueMutex;
std::condition_variable Filedata::queueCondition;
std::atomic<bool> Filedata::stopAsyncThread(false);
std::thread Filedata::asyncWriteThread;


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

std::size_t Filedata::hashQueueLimit() {
    // 1024 是"正常突发"和"攻击堆积"之间的折中：CI 基准 128 并发登录不该被拒，
    // 而随机用户名喷洒最多也只能堆 1024 个任务（约 2 线程 × 15 秒的排队）。
    static const std::size_t kDefault = 1024;
    static const std::size_t kLimit = [] {
        const char* raw = std::getenv("TETRIS_AUTH_QUEUE_LIMIT");
        if (raw == nullptr || *raw == '\0') {
            return kDefault;
        }
        char* end = nullptr;
        const long value = std::strtol(raw, &end, 10);
        if (end == nullptr || *end != '\0' || value < 1 || value > 100000) {
            return kDefault;
        }
        return static_cast<std::size_t>(value);
    }();
    return kLimit;
}

bool Filedata::runHashTask(std::function<void()> task) {
    if (!hashPoolReady.load()) {
        // 池未启用（TETRIS_HASH_THREADS=0/1）：在文件线程上串行执行，等价于旧行为
        task();
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(hashMutex);
        // 队列有界：PBKDF2 很慢，无界排队会让登录风暴把内存和延迟一起拖爆。
        // 满了直接拒绝本次校验（上层回"系统繁忙"），由客户端稍后重试。
        if (hashQueue.size() >= hashQueueLimit()) {
            return false;
        }
        hashQueue.push(task);
    }
    hashCond.notify_one();
    return true;
}

// 初始化异步系统
bool Filedata::initializeAsyncSystem() {
    // 先打开存储：失败就别起线程，让 main 直接报错退出（比"跑起来但存不了"好）
    if (!Db::openFromEnv()) {
        logger->error("打开 SQLite 存储失败（TETRIS_DB）");
        return false;
    }

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

    // 所有写任务都已执行完、文件线程已退出，可以安全关库
    Db::close();
    gPlayersLoaded = false;

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

// 异步保存方法（注册）：只把加盐哈希写进 accounts 表。
// 原实现是往 CSV 追加一行；现在一条 INSERT 就够，而且 UNIQUE(username) 天然挡住重名。
void Filedata::saveNewUserDataAsync(const shared_ptr<User>& user) {
    // 在事件循环线程上先把值拷出来：任务真正执行时这条连接可能已经断开、
    // User 对象已被放回连接池并复用（用户名/密码会被 resetUserInfo 清空）。
    const string username = user->getUsername();
    const string password = user->getPassword();

    auto task = [username, password]() {
        // 落盘只存加盐哈希，明文密码不进入数据库。
        // 先算哈希再写库：PBKDF2 要几十毫秒，不该占着 SQLite 的连接锁。
        string credential;
        try {
            credential = PasswordHash::hash(password);
        }
        catch (const std::exception& e) {
            logger->error("生成密码哈希失败，账号未保存: 用户={}, 错误={}", username, e.what());
            return;
        }

        if (!Db::insertAccount(username, credential)) {
            logger->error("账号保存失败（可能已被同名注册）: {}", username);
            return;
        }
        logger->info("账号已保存: {}", username);
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

void Filedata::update_TopScore_RecentScoreAsync(shared_ptr<User>& user) {
    auto task = [user]() {
        // 一条 INSERT 就是一个事务，不需要再拿跨进程文件锁
        Update_TopScore_RecentScore(user);
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 把某账号的密码字段改成新值。调用方在单写线程上，SQLite 自己保证原子性。
bool Filedata::rewriteStoredCredential(const std::string& username, const std::string& credential) {
    if (!Db::updateCredential(username, credential)) {
        logger->error("更新密码失败: {}", username);
        return false;
    }
    return true;
}

// 同步内存里的凭据（PlayerInfo 快照），避免本进程后续还按旧值比较
void Filedata::updateCredentialInMemory(const std::string& username, const std::string& credential) {
    auto player = PlayerInfo::findPlayer(username);
    if (player) {
        player->setPassword(credential);
    }
}

// 登录/改密只需要"某个账号的凭据"：一次主键查询（比原来"整表加载 + 缓存"更简单，
// 也不再有"缓存和文件对不上"的问题）。
bool Filedata::lookupCredential(const std::string& username, std::string& credential) {
    return Db::lookupCredential(username, credential);
}

// 登录只需要一个账号的凭据，不需要整张玩家表（见上）。
void Filedata::loginAsync(const std::string& username, const std::string& password,
    std::function<void(LoginResult)> callback) {
    // 参数全部按值捕获：任务在异步线程上跑，不能再去读 User（连接可能已断、对象已被复用）。
    auto task = [username, password, callback]() {
        // 回到事件循环线程再动 User / socket（与其它异步回调保持同一约定）
        const auto finish = [callback](LoginResult result) {
            EventLoop::post([callback, result]() { callback(result); });
            };

        string stored;
        try {
            if (!lookupCredential(username, stored)) {
                // 账号不存在：也要排一次同样代价的 PBKDF2（随机假凭据）。
                // 否则"响应很快"本身就等于告诉客户端"这个用户名不存在"，可被用来枚举账号；
                // 走哈希池而不是在文件线程上直接算，和真实校验的排队语义保持一致。
                if (!runHashTask([password, finish]() {
                        PasswordHash::verifyDummy(password);
                        finish(LoginResult::BadCredentials);
                    })) {
                    finish(LoginResult::Busy);
                }
                return;
            }
        }
        catch (const std::exception& e) {
            logger->error("登录校验异常: 用户={}, 错误={}", username, e.what());
            finish(LoginResult::BadCredentials);
            return;
        }

        // PBKDF2 是纯 CPU 计算：交给哈希线程池并行；池未启用时（0/1 线程）在这里同步执行。
        // 队列满时 runHashTask 返回 false：本次根本没校验，回 Busy，不能算成密码错误
        // （否则失败次数会被"系统繁忙"污染，退避会误伤正常用户）。
        if (!runHashTask([username, password, stored, finish]() {
                bool needsRehash = false;
                bool ok = false;
                try {
                    ok = PasswordHash::verify(password, stored, &needsRehash);
                }
                catch (const std::exception& e) {
                    logger->error("密码校验异常: 用户={}, 错误={}", username, e.what());
                    finish(LoginResult::BadCredentials);
                    return;
                }

                if (!ok) {
                    finish(LoginResult::BadCredentials);
                    return;
                }
                if (!needsRehash) {
                    finish(LoginResult::Ok);
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
                    finish(LoginResult::Ok);   // 迁移失败不影响本次登录
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    writeQueue.push([username, upgraded, finish]() {
                        if (rewriteStoredCredential(username, upgraded)) {
                            updateCredentialInMemory(username, upgraded);
                            logger->info("账号密码已迁移为加盐哈希: {}", username);
                        }
                        else {
                            logger->warn("账号密码迁移失败，保留旧值: {}", username);
                        }
                        finish(LoginResult::Ok);
                        });
                }
                queueCondition.notify_one();
            })) {
            finish(LoginResult::Busy);
        }
    };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 修改密码：和登录共用同一条"哈希线程池 + 单写线程"管线。
// 顺序很重要：**先验旧密码、再算新哈希**（不能让错的旧密码也白白烧一次 PBKDF2）。
void Filedata::changePasswordAsync(const std::string& username,
    const std::string& oldPassword, const std::string& newPassword,
    std::function<void(PasswordChangeResult)> callback) {
    // 参数全部按值捕获：任务在异步线程上跑，不能再去读 User（连接可能已断、对象已被复用）。
    auto task = [username, oldPassword, newPassword, callback]() {
        // 回到事件循环线程再动 User / socket（与其它异步回调保持同一约定）
        const auto finish = [callback](PasswordChangeResult result) {
            EventLoop::post([callback, result]() { callback(result); });
            };

        string stored;
        try {
            if (!lookupCredential(username, stored)) {
                // 已经登录成功过却查不到凭据：只可能是别的进程刚删过这一行。
                // 按"当前密码不对"回，让上层记账并提示重试。
                finish(PasswordChangeResult::BadCredentials);
                return;
            }
        }
        catch (const std::exception& e) {
            logger->error("修改密码：读取凭据异常: 用户={}, 错误={}", username, e.what());
            finish(PasswordChangeResult::Error);
            return;
        }

        if (!runHashTask([username, oldPassword, newPassword, stored, finish]() {
                // needsRehash 这里用不上：无论旧凭据是明文还是旧参数哈希，
                // 本次都会被新密码的当前格式哈希整体替换。
                bool needsRehash = false;
                bool oldOk = false;
                try {
                    oldOk = PasswordHash::verify(oldPassword, stored, &needsRehash);
                }
                catch (const std::exception& e) {
                    logger->error("修改密码：校验当前密码异常: 用户={}, 错误={}", username, e.what());
                    finish(PasswordChangeResult::Error);
                    return;
                }
                if (!oldOk) {
                    finish(PasswordChangeResult::BadCredentials);
                    return;
                }

                string upgraded;
                try {
                    upgraded = PasswordHash::hash(newPassword);
                }
                catch (const std::exception& e) {
                    logger->error("修改密码：生成新哈希失败: 用户={}, 错误={}", username, e.what());
                    finish(PasswordChangeResult::Error);
                    return;
                }

                // 落盘回单写线程：与注册/迁移的写操作串行（跨进程由 SQLite 保证）
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    writeQueue.push([username, upgraded, finish]() {
                        if (rewriteStoredCredential(username, upgraded)) {
                            updateCredentialInMemory(username, upgraded);
                            logger->info("账号密码已修改: {}", username);
                            finish(PasswordChangeResult::Ok);
                        }
                        else {
                            logger->warn("修改密码落盘失败，保留原密码: {}", username);
                            finish(PasswordChangeResult::Error);
                        }
                        });
                }
                queueCondition.notify_one();
            })) {
            finish(PasswordChangeResult::Busy);
        }
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 把 SQLite 里的账号/成绩统计装载成 PlayerInfo 快照（排行榜与 initUserInfo 用）。
// 数据没变（本进程没写过、别的进程也没提交）时直接复用，不重复重建。
bool Filedata::loadPlayerData() {
    std::lock_guard<std::mutex> loadLock(gLoadMutex);

    const std::uint64_t stamp = Db::cacheStamp();
    if (gPlayersLoaded && stamp == gPlayersStamp) {
        return true;
    }

    std::vector<Db::PlayerRow> rows;
    if (!Db::loadPlayerStats(rows)) {
        logger->error("加载玩家数据失败");
        return false;
    }

    PlayerInfo::clearPlayers();
    for (const auto& row : rows) {
        auto player = std::make_shared<PlayerInfo>();
        player->setPlayerName(row.username);
        player->setMaximum_Score_Easy(row.best[0]);
        player->setTimestampEasy(row.bestAt[0]);
        player->setMaximum_Score_Normal(row.best[1]);
        player->setTimestampNormal(row.bestAt[1]);
        player->setMaximum_Score_Difficult(row.best[2]);
        player->setTimestampDifficult(row.bestAt[2]);
        PlayerInfo::addPlayer(player);
    }

    gPlayersStamp = stamp;
    gPlayersLoaded = true;
    logger->info("从 SQLite 加载了 {} 个玩家数据", rows.size());
    return true;
}

// 最近 20 次成绩：成绩表只追加，id 就是时间顺序，倒序取最新 20 条。
vector<string> Filedata::Read_recent_grades(const shared_ptr<User>& user)
{
    vector<string> temp = {};
    if (user == nullptr) {
        return temp;
    }

    std::vector<Db::ScoreRow> rows;
    if (!Db::recentScores(user->getUsername(), 20, rows)) {
        logger->error("读取最近成绩失败: {}", user->getUsername());
        temp.push_back("-1");
        return temp;
    }

    for (const auto& row : rows) {
        temp.push_back(to_string(row.score) + "  " + row.difficulty);
    }

    if (temp.empty()) {
        temp.push_back("1");   // 用户存在但没有成绩记录（UI 依赖这个哨兵值）
    }
    return temp;
}

// 一局结束：追加一条成绩。原实现是"读整表 -> 改命中行 -> 写回临时文件 + rename"，
// 账号越多越慢；现在是一条 INSERT，顺便刷新内存里的历史最高分（本次连接展示用）。
bool Filedata::Update_TopScore_RecentScore(const shared_ptr<User>& user)
{
    if (user == nullptr) {
        logger->error("Update_TopScore_RecentScore: user 为空");
        return false;
    }

    const string username = user->getUsername();
    const string difficulty = user->getGameDiffculty();
    const int score = user->getScore();

    if (!Db::insertScore(username, difficulty, score, currenttime())) {
        logger->error("保存成绩失败: 用户={}, 难度={}, 分数={}", username, difficulty, score);
        return false;
    }

    const int best = Db::bestScore(username, difficulty);
    switch (Db::difficultyIndex(difficulty)) {
    case 0: user->setMaximum_Score_Easy(best); break;
    case 1: user->setMaximum_Score_Normal(best); break;
    case 2: user->setMaximum_Score_Diffcult(best); break;
    default: break;
    }
    return true;
}

// 🔥 新增：异步读取最近成绩（带callback）
void Filedata::readRecentGradesAsync(const shared_ptr<User>& user,std::function<void(bool success, vector<string> result)> callback) 
{
    auto task = [user, callback]() {
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
        try {
            std::vector<Db::PlayerRow> rows;
            if (!Db::loadPlayerStats(rows)) {
                logger->error("读取排行榜失败");
                EventLoop::post([callback]() { callback(false, {}); });
                return;
            }

            vector<shared_ptr<PlayerInfo>> result;
            result.reserve(rows.size());
            for (const auto& row : rows) {
                auto player = std::make_shared<PlayerInfo>();
                player->setPlayerName(row.username);
                player->setMaximum_Score_Easy(row.best[0]);
                player->setTimestampEasy(row.bestAt[0]);
                player->setMaximum_Score_Normal(row.best[1]);
                player->setTimestampNormal(row.bestAt[1]);
                player->setMaximum_Score_Difficult(row.best[2]);
                player->setTimestampDifficult(row.bestAt[2]);
                result.push_back(player);
            }

            EventLoop::post([callback, result]() { callback(true, result); });
            logger->debug("排行榜读取完成: 玩家数={}", result.size());
        }
        catch (const std::exception& e) {
            logger->error("异步读取排行榜失败: {}", e.what());
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
