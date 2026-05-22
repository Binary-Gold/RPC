#include "thread_pool/thread_pool.hpp"
#include "log_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

namespace meeting_ctrl {

struct ThreadPool::Imp {
    std::vector<std::thread> workers_;
    std::atomic<size_t> current_threads_{0};
    std::vector<std::chrono::steady_clock::time_point> thread_last_active_;

    std::priority_queue<TaskWrapper> tasks_;

    ThreadPoolStruct config_;

    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable not_full_condition_;
    bool stop_{false};

    std::atomic<ThreadPoolState> state_{ThreadPoolState::RUNNING};

    mutable std::mutex stats_mutex_;
    std::atomic<size_t> tasks_completed_{0};
    std::atomic<size_t> tasks_failed_{0};
    std::atomic<size_t> active_threads_{0};
    double total_task_time_{0.0};
};

ThreadPool::ThreadPool(size_t threads) : imp_(std::make_unique<Imp>()) {
    imp_->config_.core_threads = threads;
    imp_->config_.max_threads = threads;
    imp_->config_.max_queue_size = 0; // 无限制

    imp_->workers_.resize(threads);
    imp_->thread_last_active_.resize(threads);
    for (size_t i = 0; i <threads; ++i) {
        imp_->workers_.emplace_back(&ThreadPool::WorkerThread_, this);
        imp_->thread_last_active_[i] = std::chrono::steady_clock::now();
    }
    imp_->current_threads_ = threads;
}

ThreadPool::ThreadPool(const ThreadPoolStruct& config) : imp_(std::make_unique<Imp>()) {
    imp_->config_ = config;
    imp_->workers_.resize(imp_->config_.max_threads);
    imp_->thread_last_active_.resize(imp_->config_.max_threads);

    for (size_t i = 0; i < imp_->config_.core_threads; ++i) {
        imp_->workers_.emplace_back(&ThreadPool::WorkerThread_, this);
        imp_->thread_last_active_[i] = std::chrono::steady_clock::now();
    }
    imp_->current_threads_ = imp_->config_.core_threads;
}

ThreadPool::~ThreadPool() {
    Shutdown();
}

void ThreadPool::Pause() {

}

void ThreadPool::Resume() {

}

ThreadPool::Stats ThreadPool::GetStats() const {

}

size_t ThreadPool::Size() const noexcept {

}

size_t ThreadPool::QueueSize() const noexcept {

}

bool ThreadPool::Shutdown(std::chrono::milliseconds wait_timeout_ms) {

}

void ThreadPool::StopNow() {
    Shutdown(std::chrono::milliseconds(0));
}

ThreadPoolState ThreadPool::GetState() const noexcept {
}

void ThreadPool::WorkerThread_() {
}

void ThreadPool::AdjustThreadCount_() {
}

bool ThreadPool::Enqueue_(TaskWrapper& task) {
    {
        std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
        if (imp_->config_.max_queue_size > 0) {
            imp_->not_full_condition_.wait(lock, [this](){
                return imp_->stop_ || imp_->tasks_.size() < imp_->config_.max_queue_size;
            });
        }
        if (imp_->stop_) {
            return false;
        }
        imp_->tasks_.emplace(task);
    }
    imp_->condition_.notify_one();
}


} // namespace meeting_ctrl
