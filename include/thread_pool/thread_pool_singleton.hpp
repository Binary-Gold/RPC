#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <mutex>

#include "thread_pool.hpp"

namespace meeting_ctrl {

class ThreadPoolSingleton {
public:
    static bool Init(size_t threads = std::thread::hardware_concurrency());
    static bool Init(const ThreadPoolStruct& config);

    static ThreadPool& GetInstance();

    template<class F, class... Args>
    static auto Enqueue(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        return GetInstance().Enqueue(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    static ThreadPool::Stats GetStats();
    static size_t GetQueueSize();
    static size_t GetPoolSize();

    static bool Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    static void Destroy();
    static bool Exists();
    static ThreadPoolState GetState();

    ThreadPoolSingleton(const ThreadPoolSingleton&) = delete;
    ThreadPoolSingleton& operator=(const ThreadPoolSingleton&) = delete;

private:
    ThreadPoolSingleton() = default;
    static std::unique_ptr<ThreadPool> instance_;
    static std::mutex mutex_;
};

} // namespace meeting_ctrl
