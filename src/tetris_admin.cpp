// tetris-admin：给运维用的最小管理命令行。**不参与服务端运行**，只在运维时手工跑。
//
//   tetris-admin reset-password <user>     重置密码（交互输入两次，或 --password-stdin 从管道读）
//   tetris-admin backup [目标文件]          在线备份（SQLite Online Backup，WAL 下也一致）
//   tetris-admin list-users [--limit N]    列账号：注册时间 / 成绩条数 / 是否还是旧明文
//   tetris-admin stats                     账号数 / 成绩条数 / 各难度最高分
//
// 为什么要有它：没有邮箱，"忘记密码"没有自救路径（见 docs/HANDOVER.md 6.28），
// 以前只能手工 `sqlite3` 改库；备份也得记着一串 SQLite 命令。
//
// 设计约束（别在这里另造一套）：
//   * 密码策略用 PasswordPolicy —— 和注册、主菜单 5 改密完全相同；
//   * 落盘格式用 PasswordHash —— $pbkdf2-sha256$...，同一个 PBKDF2 参数；
//   * 表结构只有 Db.cpp 知道，这里一律走 Db 的接口。
// 所以"管理员重置出来的密码"和用户自己改出来的密码没有任何区别，也不会出现第三套格式。
//
// 服务端不用停：
//   * 改密是一条 UPDATE；服务端其它进程靠 Db::cacheStamp()（内含 SQLite data_version）
//     立刻能看到（跨进程测试已经证明过这件事，见 tests/cross_process_test.py）；
//   * 备份走 Online Backup API，运行中也能拿到一致快照（直接 cp 三件套必须在停机时做）。
//
// 退出码：0 成功；1 内部错误（打不开库/写失败/备份校验失败）；2 用法错误或目标不存在；
//         3 新密码不满足策略。

#include "TetrisGame/Db.h"
#include "TetrisGame/PasswordHash.h"
#include "TetrisGame/PasswordPolicy.h"

#include <sqlite3.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// Db.cpp 通过这个全局符号报错（服务端里它指向 log.txt）；管理工具直接写 stderr。
std::shared_ptr<spdlog::logger> logger;

namespace {

constexpr int kOk = 0;
constexpr int kInternalError = 1;
constexpr int kUsageOrMissing = 2;
constexpr int kPasswordRejected = 3;

void printUsage() {
    std::cout <<
        "用法：tetris-admin <命令> [参数]\n"
        "\n"
        "  reset-password <用户名>   重置密码（交互输入两次；--password-stdin 从管道读一行）\n"
        "  backup [目标文件]         在线备份（默认 tetris-backup-<时间>.db；--force 覆盖）\n"
        "  list-users [--limit N]    列账号（默认最近 50 个；--limit 0 显示全部）\n"
        "  stats                     账号数 / 成绩条数 / 各难度最高分\n"
        "\n"
        "环境变量：\n"
        "  TETRIS_DB   库文件路径（默认 tetris.db，必须和服务端启动目录里的那份一致）\n"
        "\n"
        "退出码：0 成功；1 内部错误；2 用法错误或目标不存在；3 新密码不满足策略\n" << std::endl;
}

// 按 UTF-8 码点算显示宽度（表格按"字符"对齐，不按字节）
std::size_t displayWidth(const std::string& text) {
    std::size_t width = 0;
    for (unsigned char byte : text) {
        if ((byte & 0xC0) != 0x80) {
            ++width;
        }
    }
    return width;
}

std::string padRight(const std::string& text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    return current >= width ? text : text + std::string(width - current, ' ');
}

std::string formatTime(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) {
        return "-";
    }
    std::time_t stamp = static_cast<std::time_t>(unixSeconds);
    std::tm* broken = std::localtime(&stamp);
    if (broken == nullptr) {
        return "-";
    }
    char buffer[32] = { 0 };
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", broken);
    return buffer;
}

std::string absolutePath(const std::string& path) {
    if (!path.empty() && path[0] == '/') {
        return path;
    }
    char cwd[4096] = { 0 };
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return path;
    }
    return std::string(cwd) + "/" + path;
}

// 从 stdin 读一行；hideEcho 时在 tty 上关掉回显（管道/CI 上没得关，直接读）。
bool readPasswordLine(const char* prompt, bool hideEcho, std::string& out) {
    std::fputs(prompt, stderr);
    std::fflush(stderr);
    bool echoDisabled = false;
    termios saved{};
    if (hideEcho && isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        termios hidden = saved;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0) {
            echoDisabled = true;
        }
    }
    const bool readOk = static_cast<bool>(std::getline(std::cin, out));
    if (echoDisabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
        std::fputs("\n", stderr);
        std::fflush(stderr);
    }
    return readOk;
}

bool openDatabaseOrReport() {
    if (Db::openFromEnv()) {
        return true;
    }
    std::cerr << "打不开数据库：" << Db::configuredPath() << "\n"
              << "  （用 TETRIS_DB 指定路径；默认路径相对当前工作目录）" << std::endl;
    return false;
}

// ---------------------------------------------------------------------------
// reset-password
// ---------------------------------------------------------------------------

int commandResetPassword(const std::string& username, bool passwordFromStdin) {
    if (username.empty()) {
        std::cerr << "reset-password 需要用户名：tetris-admin reset-password <用户名>" << std::endl;
        return kUsageOrMissing;
    }
    if (!openDatabaseOrReport()) {
        return kInternalError;
    }
    if (!Db::accountExists(username)) {
        std::cerr << "没有这个账号：" << username << "\n"
                  << "  用 `tetris-admin list-users` 查确切拼写（区分大小写）。" << std::endl;
        Db::close();
        return kUsageOrMissing;
    }

    std::string password;
    bool gotPassword = false;
    if (passwordFromStdin) {
        gotPassword = static_cast<bool>(std::getline(std::cin, password));
        if (!gotPassword) {
            std::cerr << "--password-stdin：没有从 stdin 读到内容" << std::endl;
            Db::close();
            return kUsageOrMissing;
        }
    } else {
        for (int attempt = 0; attempt < 3 && !gotPassword; ++attempt) {
            std::string first;
            std::string second;
            if (!readPasswordLine("新密码：", true, first) ||
                !readPasswordLine("再输入一次：", true, second)) {
                break;
            }
            if (first != second) {
                std::cerr << "两次输入不一致，重来（还能再试 " << (2 - attempt) << " 次）" << std::endl;
                continue;
            }
            password = first;
            gotPassword = true;
        }
        if (!gotPassword) {
            std::cerr << "已取消，密码未修改。" << std::endl;
            Db::close();
            return kUsageOrMissing;
        }
    }

    if (!PasswordPolicy::check(password)) {
        std::cerr << "新密码不满足策略：至少 " << PasswordPolicy::kMinLength
                  << " 个字符（按 UTF-8 码点计）且不能全是空白。" << std::endl;
        Db::close();
        return kPasswordRejected;
    }

    // 和当前密码相同就别动库：既避免误操作，也省一次写入。
    std::string current;
    if (Db::lookupCredential(username, current) && PasswordHash::verify(password, current)) {
        std::cout << "新密码与当前密码相同，未做修改。" << std::endl;
        Db::close();
        return kOk;
    }

    try {
        const std::string credential = PasswordHash::hash(password);
        if (!Db::updateCredential(username, credential)) {
            std::cerr << "写入新密码失败（库：" << Db::configuredPath() << "）" << std::endl;
            Db::close();
            return kInternalError;
        }
    } catch (const std::exception& error) {
        std::cerr << "生成密码哈希失败：" << error.what() << std::endl;
        Db::close();
        return kInternalError;
    }

    std::cout << "已重置 " << username << " 的密码（PBKDF2-SHA256，"
              << PasswordHash::kIterations << " 次迭代）。" << std::endl;
    std::cout << "注意：已经登录的会话不会被踢下线（服务端没有会话表），新密码从下次登录生效。"
              << std::endl;
    Db::close();
    return kOk;
}

// ---------------------------------------------------------------------------
// backup
// ---------------------------------------------------------------------------

struct SqliteHandle {
    sqlite3* db = nullptr;
    ~SqliteHandle() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }
};

bool scalarInt(sqlite3* db, const char* sql, long long& value, std::string& error) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int64(stmt, 0);
        ok = true;
    } else {
        error = sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool integrityOk(sqlite3* db, std::string& error) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        const std::string result = text != nullptr
            ? std::string(reinterpret_cast<const char*>(text)) : std::string();
        ok = (result == "ok");
        if (!ok) {
            error = result;
        }
    } else {
        error = sqlite3_errmsg(db);
    }
    sqlite3_finalize(stmt);
    return ok;
}

// 统计"备份那一刻"的快照里有多少账号/成绩；live 指针为真时统计源库当前值。
bool countRows(sqlite3* db, long long& accounts, long long& scores, std::string& error) {
    return scalarInt(db, "SELECT COUNT(*) FROM accounts", accounts, error) &&
           scalarInt(db, "SELECT COUNT(*) FROM scores", scores, error);
}

int commandBackup(std::string target, bool force) {
    const std::string source = Db::configuredPath();
    struct stat info {};
    if (stat(source.c_str(), &info) != 0) {
        std::cerr << "找不到数据库文件：" << source << "\n"
                  << "  （TETRIS_DB 或当前工作目录决定它；不要在错误的目录里跑本命令）"
                  << std::endl;
        return kUsageOrMissing;
    }

    if (target.empty()) {
        std::time_t now = std::time(nullptr);
        std::tm* broken = std::localtime(&now);
        char stamp[32] = { 0 };
        if (broken != nullptr) {
            std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", broken);
        } else {
            std::snprintf(stamp, sizeof(stamp), "%ld", static_cast<long>(now));
        }
        target = std::string("tetris-backup-") + stamp + ".db";
    }
    if (!force && access(target.c_str(), F_OK) == 0) {
        std::cerr << "目标文件已存在：" << target << "（确认覆盖就加 --force）" << std::endl;
        return kUsageOrMissing;
    }

    SqliteHandle from;
    SqliteHandle to;
    if (sqlite3_open_v2(source.c_str(), &from.db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        std::cerr << "打开源库失败：" << (from.db != nullptr ? sqlite3_errmsg(from.db) : "?")
                  << std::endl;
        return kInternalError;
    }
    sqlite3_busy_timeout(from.db, 5000);
    if (sqlite3_open_v2(target.c_str(), &to.db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::cerr << "创建备份文件失败：" << (to.db != nullptr ? sqlite3_errmsg(to.db) : "?")
                  << std::endl;
        return kInternalError;
    }

    // 备份前后各读一次源库计数：备份期间服务端可能还在写，差值是正常的，
    // 只用来在输出里说清楚"快照是哪一刻的"，不算失败。
    std::string error;
    long long liveAccounts = 0;
    long long liveScores = 0;
    const bool liveOk = countRows(from.db, liveAccounts, liveScores, error);

    sqlite3_backup* handle = sqlite3_backup_init(to.db, "main", from.db, "main");
    if (handle == nullptr) {
        std::cerr << "备份初始化失败：" << sqlite3_errmsg(to.db) << std::endl;
        return kInternalError;
    }
    const int stepRc = sqlite3_backup_step(handle, -1);
    const int finishRc = sqlite3_backup_finish(handle);
    if (stepRc != SQLITE_DONE || finishRc != SQLITE_OK) {
        std::cerr << "备份失败（step=" << stepRc << " finish=" << finishRc
                  << "）：" << sqlite3_errmsg(to.db) << std::endl;
        return kInternalError;
    }

    long long copyAccounts = 0;
    long long copyScores = 0;
    if (!countRows(to.db, copyAccounts, copyScores, error) || !integrityOk(to.db, error)) {
        std::cerr << "备份校验失败：" << error << "\n"
                  << "  文件留在 " << target << "，请勿使用。" << std::endl;
        return kInternalError;
    }

    // 库里有密码哈希，按敏感数据对待：默认只给 owner 读写。
    if (chmod(target.c_str(), 0600) != 0) {
        std::cerr << "警告：无法把 " << target << " 权限改成 600（"
                  << std::strerror(errno) << "）" << std::endl;
    }

    std::cout << "已备份 " << absolutePath(source) << "\n"
              << "     -> " << absolutePath(target) << "\n"
              << "  账号 " << copyAccounts << " 个 / 成绩 " << copyScores
              << " 条，integrity_check ok，权限 600" << std::endl;
    if (liveOk && (liveAccounts != copyAccounts || liveScores != copyScores)) {
        std::cout << "  （备份期间库里有新写入：当前 " << liveAccounts << " 个账号 / "
                  << liveScores << " 条成绩；快照是备份那一刻的一致状态）" << std::endl;
    }
    std::cout << "  备份里含密码哈希，别放进可公开下载的目录。" << std::endl;
    return kOk;
}

// ---------------------------------------------------------------------------
// list-users / stats
// ---------------------------------------------------------------------------

int commandListUsers(int limit) {
    if (!openDatabaseOrReport()) {
        return kInternalError;
    }
    std::vector<Db::AccountRow> rows;
    if (!Db::listAccounts(rows, limit)) {
        std::cerr << "读取账号列表失败" << std::endl;
        Db::close();
        return kInternalError;
    }
    if (rows.empty()) {
        std::cout << "库里还没有账号。" << std::endl;
        Db::close();
        return kOk;
    }
    std::cout << padRight("用户名", 22) << padRight("注册时间", 18)
              << padRight("成绩", 6) << "密码" << std::endl;
    for (const auto& row : rows) {
        std::cout << padRight(row.username, 22)
                  << padRight(formatTime(row.createdAt), 18)
                  << padRight(std::to_string(row.scoreCount), 6)
                  << (row.hashed ? "哈希" : "明文（未迁移）") << std::endl;
    }
    if (limit > 0 && static_cast<int>(rows.size()) == limit) {
        std::cout << "（只显示最近注册的 " << limit << " 个；--limit 0 显示全部）" << std::endl;
    }
    Db::close();
    return kOk;
}

int commandStats() {
    if (!openDatabaseOrReport()) {
        return kInternalError;
    }
    const int accounts = Db::accountCount();
    const int scores = Db::scoreCount();

    std::vector<Db::AccountRow> rows;
    int legacy = 0;
    if (Db::listAccounts(rows, 0)) {
        for (const auto& row : rows) {
            if (!row.hashed) {
                ++legacy;
            }
        }
    }

    std::cout << "账号数：" << accounts;
    if (legacy > 0) {
        std::cout << "（其中 " << legacy << " 个仍是旧明文，下次登录会自动迁移成哈希）";
    }
    std::cout << "\n成绩条数：" << scores << std::endl;

    std::vector<Db::PlayerRow> players;
    if (Db::loadPlayerStats(players) && !players.empty()) {
        std::cout << "各难度最高分：" << std::endl;
        for (int index = 0; index < 3; ++index) {
            const Db::PlayerRow* best = nullptr;
            for (const auto& player : players) {
                if (player.best[index] <= 0) {
                    continue;
                }
                if (best == nullptr || player.best[index] > best->best[index]) {
                    best = &player;
                }
            }
            std::cout << "  " << padRight(Db::difficultyName(index), 12);
            if (best == nullptr) {
                std::cout << "（还没有成绩）" << std::endl;
            } else {
                std::cout << best->best[index] << "  by " << best->username << std::endl;
            }
        }
    } else {
        std::cout << "还没有任何成绩。" << std::endl;
    }
    Db::close();
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        // 同名 logger 重复注册会抛异常，先 drop 掉（正常也只有一次）
        spdlog::drop("tetris-admin");
        logger = spdlog::stderr_color_mt("tetris-admin");
        logger->set_level(spdlog::level::warn);   // 正常输出走 stdout，库层错误走 stderr
    } catch (const std::exception& error) {
        std::cerr << "初始化日志失败：" << error.what() << std::endl;
        return kInternalError;
    }

    std::vector<std::string> args(argv + 1, argv + argc);
    for (auto it = args.begin(); it != args.end();) {
        if (*it == "-v" || *it == "--verbose") {
            logger->set_level(spdlog::level::info);
            it = args.erase(it);
        } else {
            ++it;
        }
    }

    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        printUsage();
        return args.empty() ? kUsageOrMissing : kOk;
    }

    const std::string command = args[0];
    if (command == "reset-password") {
        std::string username;
        bool passwordFromStdin = false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--password-stdin") {
                passwordFromStdin = true;
            } else if (username.empty()) {
                username = args[i];
            } else {
                std::cerr << "多余的参数：" << args[i] << std::endl;
                return kUsageOrMissing;
            }
        }
        return commandResetPassword(username, passwordFromStdin);
    }

    if (command == "backup") {
        std::string target;
        bool force = false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--force") {
                force = true;
            } else if (target.empty()) {
                target = args[i];
            } else {
                std::cerr << "多余的参数：" << args[i] << std::endl;
                return kUsageOrMissing;
            }
        }
        return commandBackup(target, force);
    }

    if (command == "list-users") {
        int limit = 50;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                limit = std::atoi(args[++i].c_str());
                if (limit < 0) {
                    std::cerr << "--limit 不能是负数" << std::endl;
                    return kUsageOrMissing;
                }
            } else {
                std::cerr << "list-users 只接受 --limit N（收到：" << args[i] << "）" << std::endl;
                return kUsageOrMissing;
            }
        }
        return commandListUsers(limit);
    }

    if (command == "stats") {
        if (args.size() > 1) {
            std::cerr << "stats 不接受参数" << std::endl;
            return kUsageOrMissing;
        }
        return commandStats();
    }

    std::cerr << "未知命令：" << command << std::endl;
    printUsage();
    return kUsageOrMissing;
}
