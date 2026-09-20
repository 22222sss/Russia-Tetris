#include"Filedata.h"

extern shared_ptr<spdlog::logger> logger;

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

// 初始化异步系统
bool Filedata::initializeAsyncSystem() {
    stopAsyncThread = false;
    asyncWriteThread = std::thread(asyncWriteWorker);

    // 预加载玩家数据到缓存
    if (!loadPlayerData()) {
        return false;
    }

    logger->info("异步文件系统初始化完成");
    return true;
}

void Filedata::shutdownAsyncSystem() {
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
    auto task = [user]() {
        ofstream file("userdata.csv", std::ios::app);
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
        file << user->getUsername() << "," << user->getPassword() << ","
            << "0" << "," << "EASY" << "," << "" << ","
            << "0" << "," << "NORMAL" << "," << "" << ","
            << "0" << "," << "DIFFICULT" << "," << "" << ",\n";

        file.close();

        // 添加到缓存
        auto player = std::make_shared<PlayerInfo>();
        player->setPlayerName(user->getUsername());
        player->setPassword(user->getPassword());
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
        Update_TopScore_RecentScore(user);
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}

// 修改loadPlayerData使用缓存
bool Filedata::loadPlayerData() {
    PlayerInfo::clearPlayers();

    // 先尝试从缓存加载
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!playerCache.empty()) {
            for (const auto& pair : playerCache) {
                PlayerInfo::addPlayer(pair.second);
            }
            logger->info("从缓存加载了 {} 个玩家数据", playerCache.size());
            return true;
        }
    }

    // 缓存为空，从文件加载
    ifstream file("userdata.csv");
    if (!file.is_open()) {
        logger->error("无法打开用户数据文件");
        return false;
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
        try {
            vector<string> result = Read_recent_grades(user);
            bool success = !(result.empty() || result.front() == "-1");

            // 通过callback返回结果
            callback(success, result);

            logger->debug("异步读取最近成绩完成: 用户={}, 记录数={}",
                user->getUsername(), result.size());
        }
        catch (const std::exception& e) {
            logger->error("异步读取最近成绩失败: 用户={}, 错误={}",
                user->getUsername(), e.what());
            callback(false, {});
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
            ifstream file("userdata.csv");
            if (!file.is_open()) {
                logger->error("无法打开用户数据文件");
                callback(false, {});
                return;
            }

            vector<shared_ptr<PlayerInfo>> result = Read_AllpalyerInfo(file);
            callback(true, result);

            logger->debug("异步读取所有玩家信息完成: 玩家数={}", result.size());
        }
        catch (const std::exception& e) {
            logger->error("异步读取所有玩家信息失败: {}", e.what());
            callback(false, {});
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
            callback(success);

            if (success) {
                logger->debug("异步加载玩家数据完成");
            }
            else {
                logger->error("异步加载玩家数据失败");
            }
        }
        catch (const std::exception& e) {
            logger->error("异步加载玩家数据异常: {}", e.what());
            callback(false);
        }
        };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        writeQueue.push(task);
    }
    queueCondition.notify_one();
}
