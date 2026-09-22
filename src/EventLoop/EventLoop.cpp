#include"EventLoop.h"

extern shared_ptr<spdlog::logger> logger;

// 初始化静态成员
std::unordered_map<evutil_socket_t, struct event*> EventLoop::events;
std::mutex EventLoop::events_mutex;
std::atomic<long> total_events_processed(0);    // 原子计数器
std::atomic<long> active_connections(0);        // 原子计数器

EventLoop::EventLoop() {
    // 使用epoll作为后端（如果可用）- 性能关键！
    struct event_config* cfg = event_config_new();
    if (cfg) {
        // 优先使用epoll，避免select/poll
        // epoll在大量连接时性能远优于select/poll
        event_config_avoid_method(cfg, "select");  // select有1024连接限制
        event_config_avoid_method(cfg, "poll");    // poll在大连接数时性能差
        base = event_base_new_with_config(cfg);
        event_config_free(cfg);
    }

    if (!base) {
        // 回退到默认配置
        base = event_base_new();
    }

    if (!base) {
        logger->error("Error initializing libevent: {} (errno: {})", strerror(errno), errno);
        logger->flush();
        throw std::runtime_error("Failed to create event base");
    }

    // 记录使用的事件机制 - 便于调试
    logger->info("EventLoop using backend: {}", event_base_get_method(base));
}

EventLoop::~EventLoop() {
    {
        std::lock_guard<std::mutex> lock(events_mutex);  // 自动加锁，离开作用域自动解锁
        for (auto& pair : events) {
            event_del(pair.second);     // 从事件循环中删除
            event_free(pair.second);    // 释放事件内存
        }
        events.clear();  // 清空map
    }

    if (base) {
        event_base_free(base);  // 释放事件基础
    }
}

void EventLoop::run() {
    if (base) {
        logger->info("EventLoop starting with {} initial events", events.size());
        event_base_dispatch(base);  // 开始事件循环（阻塞）
    }
}

bool EventLoop::registerFdEvent(evutil_socket_t fd, short event_flags, EventCallback callback, void* arg, const struct timeval* timeout) {
    struct event* ev = event_new(base, fd, event_flags, callback, arg);

    if (!ev) {
        logger->error("Error creating event for fd: {}", fd);
        return false;
    }

    // 进入作用域时：
    {
        std::lock_guard<std::mutex> lock(events_mutex); // 构造函数自动加锁
        // 临界区代码 - 这里可以安全访问共享资源
        events[fd] = ev;  // 线程安全操作
    } // 离开作用域时，lock析构函数自动解锁

    if (event_add(ev, timeout) != 0) {  // 添加到事件循环
        logger->error("Error adding event for fd: {}", fd);
        event_free(ev);
        return false;
    }

    active_connections++;  // 原子操作，线程安全
    logger->debug("Registered event for fd: {}, total events: {}", fd, events.size());
    return true;
}

bool EventLoop::batchRegisterEvents(const std::vector<evutil_socket_t>& fds, short event_flags, EventCallback callback, void* arg) {
    bool all_success = true;

    // 批量注册，减少锁竞争
    for (auto fd : fds) {
        if (!registerFdEvent(fd, event_flags, callback, arg)) {
            all_success = false;
            logger->warn("Failed to register event for fd: {}", fd);
        }
    }

    logger->info("Batch registered {} out of {} events", events.size(), fds.size());
    return all_success;
}

// ✅ 使用连接池释放
void EventLoop::unregister_Event_User(int timerfd, short events, void* arg) {
    std::lock_guard<std::mutex> lock(events_mutex);

    // 锁内遍历收集需清理的连接（forEachUser 内部持有 users_mutex，不能在回调里调用 removeUser）
    std::vector<int> toRemove;
    User::forEachUser([&](int fd, std::shared_ptr<User>& user_ptr) {
        if (user_ptr->getStatus() == STATUS_OVER_QUIT) {
            toRemove.push_back(fd);
            ConnectionPool::release(user_ptr);
        }
    });

    // 锁外移除
    for (int fd : toRemove) {
        User::removeUser(fd);
    }
}

size_t EventLoop::getActiveEventsCount() {
    std::lock_guard<std::mutex> lock(events_mutex);
    return events.size();  // 返回当前活跃事件数
}

void EventLoop::printEventStatistics() {
    std::lock_guard<std::mutex> lock(events_mutex);
    logger->info("Event Statistics - Active: {}, Processed: {}, Connections: {}",
        events.size(), total_events_processed.load(), active_connections.load());
}

std::unordered_map<evutil_socket_t, struct event*> EventLoop::getEvents() {
    std::lock_guard<std::mutex> lock(events_mutex);
    return events;  // 返回副本，避免直接暴露内部数据
}

void EventLoop::setEvents(const std::unordered_map<evutil_socket_t, struct event*>& newEvents) {
    std::lock_guard<std::mutex> lock(events_mutex);
    events = newEvents;  // 设置新的事件映射
}