#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace meeting_ctrl {

enum class TaskPriority {
    LOW,
    NORMAL,
    HIGH
};

enum class ThreadPoolState {
    RUNNING,
    PAUSED,
    SHUTTING_DOWN,
    STOPPED
};

struct ThreadPoolStruct {
    size_t core_threads = std::thread::hardware_concurrency();
    size_t max_threads = std::thread::hardware_concurrency() * 2;
    size_t max_queue_size = 1000;
    std::chrono::milliseconds keep_alive_time{60000};
};

class TaskWrapper {
public:
    TaskWrapper(std::function<void()>&& t, TaskPriority p)
        : task_(std::move(t))
        , priority_(p) {}

    TaskWrapper() : priority_(TaskPriority::NORMAL) {}

    void Execute() {
        if (Valid()) {
            task_();
        }
    }

    bool Valid() const {
        return static_cast<bool>(task_);
    }

    bool operator<(const TaskWrapper& other) const {
        return priority_ < other.priority_;
    }

private:
    std::function<void()> task_;
    TaskPriority priority_;
};

class ThreadPool {
public:
    struct Stats {
        size_t tasks_completed = 0;
        size_t tasks_failed = 0;
        double avg_task_time_ms = 0;
        size_t active_threads = 0;
    };

    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    explicit ThreadPool(const ThreadPoolStruct& config);
    ~ThreadPool();

    template<class F, class... Args>
    auto Enqueue(TaskPriority priority, F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto func = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = func->get_future();

        if (!Enqueue_(std::function<void()>([func]() { (*func)(); }), priority)) {
            return std::future<return_type>{};
        }
        return res;
    }

    void Pause();
    void Resume();

    Stats GetStats() const;
    size_t Size() const noexcept;
    size_t QueueSize() const noexcept;

    bool Shutdown(std::chrono::milliseconds wait_timeout_ms = std::chrono::milliseconds::max());
    void StopNow();

    ThreadPoolState GetState() const noexcept;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void WorkerThread_();
    void AdjustThreadCount_();
    bool Enqueue_(std::function<void()>&& func, TaskPriority priority);

    struct Imp;
    std::unique_ptr<Imp> imp_;
};

} // namespace meeting_ctrl
