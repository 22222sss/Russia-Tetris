#include"EventLoop.h"

extern shared_ptr<spdlog::logger> logger;

// 初始化静态成员
std::unordered_map<evutil_socket_t, struct event*> EventLoop::events;
std::mutex EventLoop::events_mutex;
std::atomic<long> total_events_processed(0);    // 原子计数器
std::atomic<long> active_connections(0);        // 原子计数器

EventLoop::EventLoop() {
    // 让 libevent 选用 epoll 等高效后端：避开有 1024 连接上限的 select，
    // 以及在大连接数下性能较差的 poll
    struct event_config* cfg = event_config_new();
    if (cfg) {
        event_config_avoid_method(cfg, "select");  // select 有 1024 连接限制
        event_config_avoid_method(cfg, "poll");    // poll 在大连接数时性能差
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

        // 同一个 fd 可能被内核复用给新连接。上一个连接结束时如果没有清理事件对象，
        // libevent 内部仍然认为该 fd "已在 epoll 中注册"（而内核在 close() 时已自动
        // 摘除），于是下面 event_add 不会重新 epoll_ctl(ADD)，这条连接将永远收不到
        // 可读事件 —— 表现为客户端发什么服务端都没反应。这里先彻底删掉旧事件，
        // 让 libevent 的引用计数归零，event_add 才会真正重新注册到 epoll。
        //
        // 注意：fd < 0 是定时器事件（main.cpp 用 -1 注册两个 100ms 定时器），
        // 它们不能按 fd 去重，否则会把方块下落定时器删掉。
        if (fd >= 0) {
            auto it = events.find(fd);
            if (it != events.end()) {
                event_del(it->second);
                event_free(it->second);
            }
        }

        events[fd] = ev;  // 线程安全操作
    } // 离开作用域时，lock析构函数自动解锁

    if (event_add(ev, timeout) != 0) {  // 添加到事件循环
        logger->error("Error adding event for fd: {}", fd);
        {
            // 别在 events 里留下已经释放的指针（否则后续 event_del/free 会二次释放）
            std::lock_guard<std::mutex> lock(events_mutex);
            auto it = events.find(fd);
            if (it != events.end() && it->second == ev) {
                events.erase(it);
            }
        }
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
    std::vector<int> toRemove;

    {
        std::lock_guard<std::mutex> lock(events_mutex);

        // 锁内遍历收集需清理的连接（forEachUser 内部持有 users_mutex，不能在回调里调用 removeUser）
        User::forEachUser([&](int fd, std::shared_ptr<User>& user_ptr) {
            // 非阻塞 socket 上有些输出一时写不完（部分写 / EAGAIN），
            // 这里以 100ms 的节奏补发；发送真正失败时会把状态置为 OVER_QUIT。
            if (user_ptr->hasPendingOutput()) {
                flushUserOutput(user_ptr);
            }

            if (user_ptr->getStatus() == STATUS_OVER_QUIT) {
                toRemove.push_back(fd);
                ConnectionPool::release(user_ptr);
            }
        });
    }

    // 锁外移除（removeFdEvent 会自己加 events_mutex，不能在上面的锁内调用，否则死锁）
    for (int fd : toRemove) {
        User::removeUser(fd);
        // 同时释放该 fd 的事件对象：既避免 struct event 随连接数无上限泄漏，
        // 也保证 fd 被复用后新连接能重新注册到 epoll（否则新连接会"静默"）。
        EventLoop::removeFdEvent(fd);
    }
}

void EventLoop::removeFdEvent(evutil_socket_t fd) {
    if (fd < 0) {   // 定时器事件（fd = -1）不参与按 fd 的清理
        return;
    }
    std::lock_guard<std::mutex> lock(events_mutex);
    auto it = events.find(fd);
    if (it == events.end()) {
        return;
    }
    event_del(it->second);
    event_free(it->second);
    events.erase(it);
    if (active_connections > 0) {
        active_connections--;
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