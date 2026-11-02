#pragma once
#ifndef Filedata_H
#define Filedata_H

#include"User.h"
#include"Game.h"
#include"Filedata.h"
#include"PlayerInfo.h"
#include"../Common/Common.h"
#include"../Utility/Utility.h"
#include"../UImanage/UImanage.h"
#include"../EventLoop/EventLoop.h"

class Filedata
{
private:
	// 异步写入队列
	static std::queue<std::function<void()>> writeQueue;  // 任务队列
	static std::mutex queueMutex;                         // 队列锁
	static std::condition_variable queueCondition;        // 条件变量
	static std::atomic<bool> stopAsyncThread;             // 停止标志
	static std::thread asyncWriteThread;                  // 后台线程

	// 内存缓存
	static std::unordered_map<std::string, std::shared_ptr<PlayerInfo>> playerCache;  // 缓存映射
	static std::mutex cacheMutex;                           // 缓存锁
	static std::atomic<long> cacheHits;                     // 命中统计
	static std::atomic<long> cacheMisses;                   // 未命中统计

	// 密码校验线程池：PBKDF2 是纯 CPU 计算，可以并行；默认 2 线程。
	// TETRIS_HASH_THREADS=0/1 可关掉（校验退回文件线程串行执行，用于对照实验）。
	// 线程只能在 fork 之后创建（initializeAsyncSystem 里），否则 worker 进程里没有这些线程。
	static std::vector<std::thread> hashThreads;
	static std::queue<std::function<void()>> hashQueue;
	static std::mutex hashMutex;
	static std::condition_variable hashCond;
	static std::atomic<bool> hashStop;
	static std::atomic<bool> hashPoolReady;

	// 异步写入线程函数
	static void asyncWriteWorker();

	static int hashThreadCount();
	static void ensureHashPool();
	static void stopHashPool();
	static void hashWorker();
	// 有池就投递到池，没有就同步执行（同一份任务代码）。
	// 返回 false 表示池里待处理任务已到上限、本次没有执行（上层回"系统繁忙"）。
	static bool runHashTask(std::function<void()> task);
	// 哈希队列待处理上限（TETRIS_AUTH_QUEUE_LIMIT，默认 1024）：
	// PBKDF2 很慢，无界排队会让登录风暴把内存和延迟一起拖爆。
	// 1024 约等于"2 线程 × 15 秒"的最坏排队，远高于正常突发（CI 的 128 并发登录），
	// 又能挡住随机用户名喷洒的无界堆积。
	static std::size_t hashQueueLimit();

	// 缓存管理
	static void addToCache(const std::shared_ptr<PlayerInfo>& player);
	static std::shared_ptr<PlayerInfo> getFromCache(const std::string& username);
	static void removeFromCache(const std::string& username);

	// 把某一行的 Password 字段原地替换成新值（调用方必须已持有 DataFileLock 独占锁）
	static bool rewriteStoredCredential(const std::string& username, const std::string& credential);
	// 迁移后同步内存里的明文/旧哈希，避免本进程后续登录仍按旧值校验
	static void updateCredentialInMemory(const std::string& username, const std::string& credential);
	// 只查一个用户的凭据：缓存有效时 O(1)，不再为每次登录重建整个 PlayerInfo
	static bool lookupCredential(const std::string& username, std::string& credential);

public:
	// 登录校验结果：Busy 表示服务端哈希队列已满、本次根本没校验（不能算"密码错误"，
	// 否则失败次数会被系统繁忙污染，退避会误伤正常用户）。
	enum class LoginResult {
		Ok,
		BadCredentials,
		Busy
	};

	// 修改密码的结果。与登录分开是因为多了一个"内部错误"：
	// 哈希/写盘失败既不是"密码错"（不能计入退避），也不是"忙"（用户重试也没用）。
	//   Ok 成功；BadCredentials 当前密码不对（要计入退避与按 IP 兜底）；
	//   Busy 哈希队列满、本次没校验；Error 生成哈希或落盘失败。
	enum class PasswordChangeResult {
		Ok,
		BadCredentials,
		Busy,
		Error
	};

	// 初始化异步写入系统
	static bool initializeAsyncSystem();
	static void shutdownAsyncSystem();


	static vector<string> Read_recent_grades(const shared_ptr<User>& user);

	static vector<shared_ptr<PlayerInfo>> Read_AllpalyerInfo(ifstream &file);

	// 读取用户数据文件，初始化players向量
	static bool loadPlayerData();

	//更新用户最高分和最近20次分数
	static bool Update_TopScore_RecentScore(const shared_ptr<User>& user);

	// 异步保存方法
	
	// 写入用户数据文件，保存更新后的用户数据
	static void saveNewUserDataAsync(const shared_ptr<User>& user);
	//更新用户最高分和最近20次分数
	static void update_TopScore_RecentScoreAsync(shared_ptr<User>& user);

	// 校验密码（异步，PBKDF2 有意较慢，不能占事件循环线程）。
	// 校验通过且存储的是旧版明文/旧参数哈希时，顺手迁移成当前格式。
	// callback 在事件循环线程上执行，参数是本次校验结果（Ok/BadCredentials/Busy）。
	static void loginAsync(const std::string& username, const std::string& password,
		std::function<void(LoginResult)> callback);

	// 修改密码：校验当前密码 -> 生成新密码的加盐哈希 -> 在 flock 临界区内原地改写
	// 该行（临时文件 + rename，多进程安全）-> 同步本进程缓存。
	// 旧密码校验失败回 BadCredentials（上层必须计入退避，否则改密流程就成了
	// 绕过登录限流猜密码的旁路）；callback 在事件循环线程上执行。
	static void changePasswordAsync(const std::string& username,
		const std::string& oldPassword, const std::string& newPassword,
		std::function<void(PasswordChangeResult)> callback);

	// 🔥 新增：异步读取方法（带callback）
	static void readRecentGradesAsync(const shared_ptr<User>& user,std::function<void(bool success, vector<string> result)> callback);

	static void readAllPlayerInfoAsync(std::function<void(bool success, vector<shared_ptr<PlayerInfo>> result)> callback);

	static void loadPlayerDataAsync(std::function<void(bool success)> callback);


	// 缓存统计
	static void printCacheStatistics();

};

#endif