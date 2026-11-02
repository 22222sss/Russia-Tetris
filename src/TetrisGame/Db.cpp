#include "Db.h"

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <spdlog/spdlog.h>

extern std::shared_ptr<spdlog::logger> logger;

namespace {

sqlite3* gDb = nullptr;
std::string gPath;
// 单连接 + 递归锁：所有查询都是微秒级；慢操作（PBKDF2）在调用方、锁外做。
std::recursive_mutex gMutex;
// 本进程提交过多少次写：data_version 只反映"别的连接"的提交，
// 本进程自己的写入要靠这个计数让玩家表快照失效。
std::atomic<std::uint64_t> gLocalWrites{ 0 };

const char* const kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS accounts (
    username   TEXT PRIMARY KEY,
    password   TEXT NOT NULL,
    created_at INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS scores (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    username   TEXT NOT NULL,
    difficulty TEXT NOT NULL,
    score      INTEGER NOT NULL,
    played_at  TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_scores_user ON scores(username, id DESC);
CREATE INDEX IF NOT EXISTS idx_scores_best ON scores(username, difficulty, score DESC, id ASC);
CREATE INDEX IF NOT EXISTS idx_scores_top  ON scores(difficulty, score DESC, id ASC);
)SQL";

// 排行榜：每个账号三个难度的最高分 + 对应时间（只返回至少有一条成绩的账号）。
// 子查询各走 idx_scores_best，账号数上千也是毫秒级。
const char* const kPlayerStatsSql =
"SELECT a.username,"
" COALESCE((SELECT s.score FROM scores s WHERE s.username=a.username AND s.difficulty='EASY'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),0),"
" COALESCE((SELECT s.played_at FROM scores s WHERE s.username=a.username AND s.difficulty='EASY'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),''),"
" COALESCE((SELECT s.score FROM scores s WHERE s.username=a.username AND s.difficulty='NORMAL'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),0),"
" COALESCE((SELECT s.played_at FROM scores s WHERE s.username=a.username AND s.difficulty='NORMAL'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),''),"
" COALESCE((SELECT s.score FROM scores s WHERE s.username=a.username AND s.difficulty='DIFFICULT'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),0),"
" COALESCE((SELECT s.played_at FROM scores s WHERE s.username=a.username AND s.difficulty='DIFFICULT'"
"   ORDER BY s.score DESC, s.id ASC LIMIT 1),'')"
" FROM accounts a"
" WHERE EXISTS (SELECT 1 FROM scores x WHERE x.username=a.username)"
" ORDER BY a.username";

bool execLocked(const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(gDb, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        logger->error("SQLite 执行失败: {}", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

// sqlite3_stmt 的 RAII 包装：prepare 之后不用手工 finalize
class Stmt {
public:
    Stmt() = default;
    ~Stmt() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool prepare(const char* sql) {
        return sqlite3_prepare_v2(gDb, sql, -1, &stmt_, nullptr) == SQLITE_OK;
    }
    bool bindText(int index, const std::string& value) {
        return sqlite3_bind_text(stmt_, index, value.c_str(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool bindInt(int index, int value) {
        return sqlite3_bind_int(stmt_, index, value) == SQLITE_OK;
    }
    bool bindInt64(int index, sqlite3_int64 value) {
        return sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK;
    }
    int step() {
        return sqlite3_step(stmt_);
    }
    std::string columnText(int index) const {
        const unsigned char* text = sqlite3_column_text(stmt_, index);
        return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
    }
    int columnInt(int index) const {
        return sqlite3_column_int(stmt_, index);
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

bool getMetaLocked(const std::string& key, std::string& out) {
    Stmt st;
    if (!st.prepare("SELECT value FROM meta WHERE key=?")) {
        return false;
    }
    st.bindText(1, key);
    if (st.step() != SQLITE_ROW) {
        return false;
    }
    out = st.columnText(0);
    return true;
}

bool setMetaLocked(const std::string& key, const std::string& value) {
    Stmt st;
    if (!st.prepare("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)")) {
        return false;
    }
    st.bindText(1, key);
    st.bindText(2, value);
    return st.step() == SQLITE_DONE;
}

int accountCountLocked() {
    Stmt st;
    if (!st.prepare("SELECT COUNT(*) FROM accounts") || st.step() != SQLITE_ROW) {
        return -1;
    }
    return st.columnInt(0);
}

int scoreCountLocked() {
    Stmt st;
    if (!st.prepare("SELECT COUNT(*) FROM scores") || st.step() != SQLITE_ROW) {
        return -1;
    }
    return st.columnInt(0);
}

bool insertAccountLocked(const std::string& username, const std::string& credential) {
    Stmt st;
    if (!st.prepare("INSERT INTO accounts(username,password,created_at) VALUES(?,?,?)")) {
        return false;
    }
    st.bindText(1, username);
    st.bindText(2, credential);
    st.bindInt64(3, static_cast<sqlite3_int64>(std::time(nullptr)));

    const int rc = st.step();
    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            // 并发注册同一个名字：SQLite 的 UNIQUE 约束兜住，不会像 CSV 那样写重
            logger->warn("账号已存在，忽略重复注册: {}", username);
        } else {
            logger->error("写入账号失败: {}（{}）", username, sqlite3_errmsg(gDb));
        }
        return false;
    }
    gLocalWrites++;
    return true;
}

bool insertScoreLocked(const std::string& username, const std::string& difficulty,
    int score, const std::string& playedAt) {
    Stmt st;
    if (!st.prepare("INSERT INTO scores(username,difficulty,score,played_at) VALUES(?,?,?,?)")) {
        return false;
    }
    st.bindText(1, username);
    st.bindText(2, difficulty);
    st.bindInt(3, score);
    st.bindText(4, playedAt);

    if (st.step() != SQLITE_DONE) {
        logger->error("写入成绩失败: {}（{}）", username, sqlite3_errmsg(gDb));
        return false;
    }
    gLocalWrites++;
    return true;
}

// 旧 userdata.csv 导入（调用方已持锁、且已确认 DB 是空的）
int importCsvLocked(const std::string& csvPath) {
    std::ifstream in(csvPath);
    if (!in.is_open()) {
        return 0;
    }

    // 逐条 autocommit 在几万账号时会很慢，包一个事务
    execLocked("BEGIN IMMEDIATE");

    int accounts = 0;
    int scores = 0;
    std::string line;
    std::getline(in, line);   // 表头

    // 列序：用户名,密码,EASY分数,EASY,时间,NORMAL分数,NORMAL,时间,DIFFICULT分数,DIFFICULT,时间,最近成绩对...
    const int kScoreIdx[3] = { 2, 5, 8 };
    const int kTimeIdx[3] = { 4, 7, 10 };
    const char* const kNames[3] = { "EASY", "NORMAL", "DIFFICULT" };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> cells;
        std::string cell;
        std::istringstream ss(line);
        while (std::getline(ss, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() < 2 || cells[0].empty()) {
            continue;
        }

        const std::string& username = cells[0];
        const std::string& credential = cells[1];   // 明文就按明文导入，登录时会迁移

        if (insertAccountLocked(username, credential)) {
            ++accounts;
        }

        const int cellCount = static_cast<int>(cells.size());
        for (int i = 0; i < 3; ++i) {
            if (kScoreIdx[i] >= cellCount) {
                continue;
            }
            const int score = std::atoi(cells[kScoreIdx[i]].c_str());
            if (score <= 0) {
                continue;
            }
            const std::string playedAt =
                (kTimeIdx[i] < cellCount) ? cells[kTimeIdx[i]] : std::string();
            if (insertScoreLocked(username, kNames[i], score, playedAt)) {
                ++scores;
            }
        }

        // 最近成绩从第 12 个字段（下标 11）开始成对出现
        for (std::size_t i = 11; i + 1 < cells.size(); i += 2) {
            if (cells[i].empty() || cells[i + 1].empty()) {
                continue;
            }
            const int score = std::atoi(cells[i].c_str());
            if (score < 0) {
                continue;
            }
            if (insertScoreLocked(username, cells[i + 1], score, "")) {
                ++scores;
            }
        }
    }

    execLocked("COMMIT");
    setMetaLocked("schema_version", "1");
    logger->info("已从 {} 导入 {} 个账号 / {} 条成绩", csvPath, accounts, scores);
    return accounts;
}

}  // namespace

int Db::difficultyIndex(const std::string& difficulty) {
    if (difficulty == "EASY") {
        return 0;
    }
    if (difficulty == "NORMAL") {
        return 1;
    }
    if (difficulty == "DIFFICULT") {
        return 2;
    }
    return -1;
}

const char* Db::difficultyName(int index) {
    switch (index) {
    case 0: return "EASY";
    case 1: return "NORMAL";
    case 2: return "DIFFICULT";
    default: return "";
    }
}

bool Db::openFromEnv() {
    const char* raw = std::getenv("TETRIS_DB");
    std::string path = (raw != nullptr && *raw != '\0') ? std::string(raw) : std::string("tetris.db");
    return open(path);
}

bool Db::open(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    return openLocked(path);
}

bool Db::openLocked(const std::string& path) {
    if (gDb) {
        return true;
    }
    gPath = path.empty() ? std::string("tetris.db") : path;

    const int rc = sqlite3_open_v2(gPath.c_str(), &gDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        logger->error("打开 SQLite 失败: {}（{}）", gPath,
            gDb ? sqlite3_errmsg(gDb) : "无法分配连接");
        if (gDb) {
            sqlite3_close(gDb);
            gDb = nullptr;
        }
        return false;
    }

    sqlite3_busy_timeout(gDb, 5000);   // 多进程抢写时最多等 5 秒
    // 多 worker 会在同一瞬间打开同一个库（都执行 WAL + 建表）。busy_timeout 已经能
    // 挡住大部分竞争，这里再重试两次，避免"两个进程同时首次启动"时偶发初始化失败。
    bool ready = false;
    for (int attempt = 0; attempt < 3 && !ready; ++attempt) {
        ready = execLocked("PRAGMA journal_mode=WAL") && execLocked(kSchema);
        if (!ready && attempt + 1 < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if (!ready) {
        logger->error("初始化 SQLite 表结构失败: {}", gPath);
        closeLocked();
        return false;
    }
    execLocked("PRAGMA synchronous=NORMAL");
    execLocked("PRAGMA foreign_keys=ON");

    // 旧 CSV 一次性导入：只在"新库 + 还没导入过"时做。
    // 没找到 CSV 就不落标记，这样之后把文件放进来重启还能导入。
    std::string flag;
    if (!getMetaLocked("csv_imported", flag)) {
        if (accountCountLocked() > 0) {
            setMetaLocked("csv_imported", "1");
        } else {
            std::ifstream legacy("userdata.csv");
            if (legacy.good()) {
                legacy.close();
                importCsvLocked("userdata.csv");
                setMetaLocked("csv_imported", "1");
            }
        }
    }

    logger->info("SQLite 存储已就绪: {}（账号 {} 个，成绩 {} 条）",
        gPath, accountCountLocked(), scoreCountLocked());
    return true;
}

void Db::close() {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    closeLocked();
}

void Db::closeLocked() {
    if (!gDb) {
        return;
    }
    sqlite3_close(gDb);
    gDb = nullptr;
    logger->info("SQLite 存储已关闭");
}

bool Db::ready() {
    return gDb != nullptr;
}

std::string Db::path() {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    return gPath;
}

std::uint64_t Db::cacheStamp() {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    int dataVersion = 0;
    if (gDb) {
        Stmt st;
        if (st.prepare("PRAGMA data_version") && st.step() == SQLITE_ROW) {
            dataVersion = st.columnInt(0);
        }
    }
    const std::uint64_t local = gLocalWrites.load();
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(dataVersion)) << 32)
        | (local & 0xFFFFFFFFULL);
}

bool Db::accountExists(const std::string& username) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty()) {
        return false;
    }
    Stmt st;
    if (!st.prepare("SELECT 1 FROM accounts WHERE username=? LIMIT 1")) {
        return false;
    }
    st.bindText(1, username);
    return st.step() == SQLITE_ROW;
}

bool Db::lookupCredential(const std::string& username, std::string& credential) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty()) {
        return false;
    }
    Stmt st;
    if (!st.prepare("SELECT password FROM accounts WHERE username=? LIMIT 1")) {
        return false;
    }
    st.bindText(1, username);
    if (st.step() != SQLITE_ROW) {
        return false;
    }
    credential = st.columnText(0);
    return true;
}

bool Db::insertAccount(const std::string& username, const std::string& credential) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty()) {
        return false;
    }
    return insertAccountLocked(username, credential);
}

bool Db::updateCredential(const std::string& username, const std::string& credential) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty()) {
        return false;
    }
    Stmt st;
    if (!st.prepare("UPDATE accounts SET password=? WHERE username=?")) {
        return false;
    }
    st.bindText(1, credential);
    st.bindText(2, username);
    if (st.step() != SQLITE_DONE) {
        logger->error("更新密码失败: {}（{}）", username, sqlite3_errmsg(gDb));
        return false;
    }
    if (sqlite3_changes(gDb) == 0) {
        logger->warn("更新密码时没找到账号: {}", username);
        return false;
    }
    gLocalWrites++;
    return true;
}

int Db::accountCount() {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb) {
        return 0;
    }
    return accountCountLocked();
}

int Db::scoreCount() {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb) {
        return 0;
    }
    return scoreCountLocked();
}

bool Db::insertScore(const std::string& username, const std::string& difficulty,
    int score, const std::string& playedAt) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty() || difficultyIndex(difficulty) < 0) {
        return false;
    }
    return insertScoreLocked(username, difficulty, score, playedAt);
}

bool Db::recentScores(const std::string& username, int limit,
    std::vector<ScoreRow>& out) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    out.clear();
    if (!gDb || username.empty()) {
        return false;
    }
    if (limit <= 0) {
        limit = 20;
    }
    Stmt st;
    if (!st.prepare("SELECT score, difficulty FROM scores WHERE username=?"
        " ORDER BY id DESC LIMIT ?")) {
        return false;
    }
    st.bindText(1, username);
    st.bindInt(2, limit);
    while (st.step() == SQLITE_ROW) {
        ScoreRow row;
        row.score = st.columnInt(0);
        row.difficulty = st.columnText(1);
        out.push_back(row);
    }
    return true;
}

bool Db::loadPlayerStats(std::vector<PlayerRow>& out) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    out.clear();
    if (!gDb) {
        return false;
    }
    Stmt st;
    if (!st.prepare(kPlayerStatsSql)) {
        return false;
    }
    while (st.step() == SQLITE_ROW) {
        PlayerRow row;
        row.username = st.columnText(0);
        for (int i = 0; i < 3; ++i) {
            row.best[i] = st.columnInt(1 + i * 2);
            row.bestAt[i] = st.columnText(2 + i * 2);
        }
        out.push_back(row);
    }
    return true;
}

int Db::bestScore(const std::string& username, const std::string& difficulty) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb || username.empty()) {
        return 0;
    }
    Stmt st;
    if (!st.prepare("SELECT COALESCE(MAX(score),0) FROM scores WHERE username=? AND difficulty=?")) {
        return 0;
    }
    st.bindText(1, username);
    st.bindText(2, difficulty);
    if (st.step() != SQLITE_ROW) {
        return 0;
    }
    return st.columnInt(0);
}

int Db::importLegacyCsv(const std::string& csvPath) {
    std::lock_guard<std::recursive_mutex> lock(gMutex);
    if (!gDb) {
        return -1;
    }
    if (accountCountLocked() > 0) {
        return 0;
    }
    return importCsvLocked(csvPath);
}
