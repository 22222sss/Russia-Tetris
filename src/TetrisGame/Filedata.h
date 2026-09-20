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

	// 异步写入线程函数
	static void asyncWriteWorker();

	// 缓存管理
	static void addToCache(const std::shared_ptr<PlayerInfo>& player);
	static std::shared_ptr<PlayerInfo> getFromCache(const std::string& username);
	static void removeFromCache(const std::string& username);

public:
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

	// 🔥 新增：异步读取方法（带callback）
	static void readRecentGradesAsync(const shared_ptr<User>& user,std::function<void(bool success, vector<string> result)> callback);

	static void readAllPlayerInfoAsync(std::function<void(bool success, vector<shared_ptr<PlayerInfo>> result)> callback);

	static void loadPlayerDataAsync(std::function<void(bool success)> callback);


	// 缓存统计
	static void printCacheStatistics();

};

#endif