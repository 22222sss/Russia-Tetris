#pragma once
#ifndef Db_H
#define Db_H

#include <cstdint>
#include <string>
#include <vector>

// SQLite 存储层：账号（密码哈希）+ 成绩。
//
// 为什么换掉 CSV：注册是追加一行、但**改密/成绩更新要整表重写**（临时文件 + rename），
// 而且每个进程的缓存一失效就要把整张表重新解析一遍；账号和成绩一多，写放大和解析
// 成本都是 O(n)，多进程还要靠 flock 串行。SQLite 用一条 UPDATE/INSERT 就能完成，
// WAL 下多进程可以并发读、写按行加锁；排行榜也能直接用索引取前 N。
//
// 约定：
//   * 只有一个连接（每进程一个），所有访问由内部递归锁串行化——查询都是微秒级，
//     真正的慢操作（PBKDF2）在调用方、锁外做；
//   * 难度的三个下标固定为 0=EASY 1=NORMAL 2=DIFFICULT（与 CSV 里的写法一致）；
//   * 成绩表只追加（每局一行），"最近 20 次"用索引 ORDER BY id DESC LIMIT 20 取；
//   * 旧 userdata.csv 在首次打开空库时一次性导入（见 importLegacyCsv）。
class Db {
public:
    struct ScoreRow {
        int score = 0;
        std::string difficulty;
    };

    // 排行榜用：每个账号三个难度的最高分与对应时间（只包含至少有一条成绩的账号）
    struct PlayerRow {
        std::string username;
        int best[3] = { 0, 0, 0 };
        std::string bestAt[3];
    };

    // 难度名字 <-> 下标；未知名字返回 -1 / 空串
    static int difficultyIndex(const std::string& difficulty);
    static const char* difficultyName(int index);

    // 管理命令行用：账号列表（用户名 + 注册时间 + 成绩条数 + 是否已迁移成哈希）。
    // 只读，且只给运维看；`hashed=false` 表示这个账号还是旧版明文、一次都还没登录过。
    struct AccountRow {
        std::string username;
        std::int64_t createdAt = 0;   // unix 秒（0 = 未知）
        int scoreCount = 0;
        bool hashed = false;
    };
    static bool listAccounts(std::vector<AccountRow>& out, int limit = 0);

    // 打开数据库（必要时建库/建表/导入旧 CSV）。
    // path 为空时读 TETRIS_DB（默认 "tetris.db"）。
    static bool openFromEnv();
    static bool open(const std::string& path);
    static void close();
    static bool ready();
    static std::string path();
    // TETRIS_DB 或默认路径；open 之前也能用（启动时打印生效配置用）
    static std::string configuredPath();

    // 玩家表快照的失效标记：本进程写次数 + SQLite data_version（别的进程提交过就会变）。
    static std::uint64_t cacheStamp();

    static bool accountExists(const std::string& username);
    static bool lookupCredential(const std::string& username, std::string& credential);
    static bool insertAccount(const std::string& username, const std::string& credential);
    static bool updateCredential(const std::string& username, const std::string& credential);
    static int accountCount();

    static bool insertScore(const std::string& username, const std::string& difficulty,
        int score, const std::string& playedAt);
    static bool recentScores(const std::string& username, int limit,
        std::vector<ScoreRow>& out);
    static bool loadPlayerStats(std::vector<PlayerRow>& out);
    static int bestScore(const std::string& username, const std::string& difficulty);
    static int scoreCount();

    // 旧 userdata.csv 一次性导入；返回导入的账号数，<0 表示失败。
    static int importLegacyCsv(const std::string& csvPath);

private:
    // 内部实现都在 Db.cpp；这里只暴露"是否已打开"和路径，避免把 sqlite3.h 带进头文件
    static bool openLocked(const std::string& path);
    static void closeLocked();
};

#endif
