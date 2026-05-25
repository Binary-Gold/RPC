#include "thread_pool/thread_pool.hpp"
#include "log_manager.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
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

    imp_->workers_.reserve(threads);
    imp_->thread_last_active_.reserve(threads);
    for (size_t i = 0; i <threads; ++i) {
        imp_->workers_.emplace_back(&ThreadPool::WorkerThread_, this);
        imp_->thread_last_active_[i] = std::chrono::steady_clock::now();
    }
    imp_->current_threads_ = threads;
}

ThreadPool::ThreadPool(const ThreadPoolStruct& config) : imp_(std::make_unique<Imp>()) {
    imp_->config_ = config;
    imp_->workers_.reserve(imp_->config_.max_threads);
    imp_->thread_last_active_.reserve(imp_->config_.max_threads);

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
    auto expected = ThreadPoolState::RUNNING;
    if(imp_->state_.compare_exchange_strong(expected, ThreadPoolState::PAUSED)) {
        imp_->condition_.notify_all();
    }
}

void ThreadPool::Resume() {
    auto expected = ThreadPoolState::PAUSED;
    if(imp_->state_.compare_exchange_strong(expected, ThreadPoolState::RUNNING)) {
        imp_->condition_.notify_all();
    }
}

ThreadPool::Stats ThreadPool::GetStats() const {
    Stats stats;
    stats.tasks_completed = imp_->tasks_completed_;
    stats.tasks_failed = imp_->tasks_failed_;
    stats.active_threads = imp_->active_threads_;
    
    std::lock_guard<std::mutex> lock(imp_->stats_mutex_);
    if(imp_->tasks_completed_ > 0) {
        stats.avg_task_time_ms = imp_->total_task_time_ / imp_->tasks_completed_;
    }
    
    return stats;
}

size_t ThreadPool::Size() const noexcept {
    return imp_->workers_.size();
}

size_t ThreadPool::QueueSize() const noexcept {
    std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
    return imp_->tasks_.size();
}

bool ThreadPool::Shutdown(std::chrono::milliseconds wait_timeout_ms) {
    ThreadPoolState expected = ThreadPoolState::RUNNING;

    if(!imp_->state_.compare_exchange_strong(expected, ThreadPoolState::SHUTTING_DOWN)) {
        return imp_->state_ == ThreadPoolState::STOPPED;
    }
    
    imp_->condition_.notify_all();
    
    {
        std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
        bool tasks_completed = imp_->condition_.wait_for(lock, wait_timeout_ms, [this] {
            return imp_->tasks_.empty() && imp_->active_threads_ == 0;
        });
        
        if(!tasks_completed) {
            return false;
        }
        
        imp_->state_ = ThreadPoolState::STOPPED;
        imp_->stop_ = true;
    }
    
    imp_->condition_.notify_all();
    
    for(std::thread &worker: imp_->workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
    
    return true;
}

void ThreadPool::StopNow() {
        {
        std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
        imp_->state_ = ThreadPoolState::STOPPED;
        imp_->stop_ = true;
        
        while(!imp_->tasks_.empty()) {
            imp_->tasks_.pop();
        }
    }
    
    imp_->condition_.notify_all();
    
    for(std::thread &worker: imp_->workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

ThreadPoolState ThreadPool::GetState() const noexcept {
    return imp_->state_;
}

void ThreadPool::WorkerThread_() {
    std::thread::id thread_id = std::this_thread::get_id();
    std::ostringstream oss;
    oss << thread_id;
    std::string thread_id_str = oss.str();
    
    auto last_active = std::chrono::steady_clock::now();

    while (true) {
        TaskWrapper task_wrapper{};
        {
            std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
            auto wait_time = imp_->config_.keep_alive_time;
            bool has_task = imp_->condition_.wait_for(lock, wait_time, [this](){
                return imp_->stop_ || 
                        (imp_->state_ == ThreadPoolState::RUNNING && !imp_->tasks_.empty()) ||
                        (imp_->state_ == ThreadPoolState::SHUTTING_DOWN && !imp_->tasks_.empty());
            });
            if (!has_task && imp_->current_threads_ > imp_->config_.core_threads) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_active > imp_->config_.keep_alive_time) {
                    imp_->current_threads_--;
                    LOG_INFO("[ThreadPool] Worker thread {} exiting due to timeout, remaining threads: {}", 
                            thread_id_str, imp_->current_threads_.load());
                    // todo 缩小对workers的影响当前处理为提前退出但不清理尸体
                    return;
                }
            }
            if ((imp_->stop_ || imp_->state_ == ThreadPoolState::STOPPED) && imp_->tasks_.empty()) {
                return;
            }
            if (imp_->state_ == ThreadPoolState::PAUSED) {
                // todo 改成信号触发
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (!imp_->tasks_.empty()) {
                task_wrapper = std::move(const_cast<TaskWrapper&>(imp_->tasks_.top()));
                imp_->tasks_.pop();
                last_active = std::chrono::steady_clock::now();

                LOG_INFO("[ThreadPool] Thread {} picked up task, queue size: {}, active threads: {}", 
                    thread_id_str, imp_->tasks_.size(), imp_->active_threads_.load() + 1);
                
                if (imp_->config_.max_queue_size > 0 && imp_->tasks_.size() < imp_->config_.max_queue_size) {
                    // 通知队列不满
                    imp_->not_full_condition_.notify_one();
                }
            }
        }
        if (task_wrapper.Valid()) {
            auto start_time = std::chrono::high_resolution_clock::now();
            imp_->active_threads_++;
            
            try {
                task_wrapper.Execute();
                imp_->tasks_completed_++;

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                double duration_ms = duration.count() / 1000.0;

                LOG_INFO("[ThreadPool] Thread {} completed task successfully in {}ms, total completed: {}", 
                        thread_id_str, duration_ms, imp_->tasks_completed_.load());
            } catch (const std::exception& e) {
                imp_->tasks_failed_++;
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                double duration_ms = duration.count() / 1000.0;
                
                LOG_INFO("[ThreadPool] Thread {} task failed after {}ms, error: {}, total failed: {}", 
                            thread_id_str, duration_ms, e.what(), imp_->tasks_failed_.load());
            }
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            {
                std::lock_guard<std::mutex> lock(imp_->stats_mutex_);
                imp_->total_task_time_ += duration.count() / 1000.0;
            }
            
            imp_->active_threads_--; 
            
            if(imp_->state_ == ThreadPoolState::SHUTTING_DOWN && imp_->active_threads_ == 0 && imp_->tasks_.empty()) {
                imp_->condition_.notify_all();
            }
        }
    }
}

void ThreadPool::AdjustThreadCount_() {
    if(imp_->current_threads_ < imp_->config_.max_threads && imp_->tasks_.size() > imp_->active_threads_) {
        size_t new_thread_id = imp_->current_threads_++;
        if(new_thread_id < imp_->thread_last_active_.size()) {
            imp_->thread_last_active_[new_thread_id] = std::chrono::steady_clock::now();
        }
        
        try {
            imp_->workers_.emplace_back(&ThreadPool::WorkerThread_, this); 
        } catch(const std::exception& e) {
            imp_->current_threads_--;
            LOG_ERROR("[ThreadPool] Failed to create worker thread: {}", e.what());
        }
    }
}

bool ThreadPool::Enqueue_(std::function<void()>&& func, TaskPriority priority) {
    {
        std::unique_lock<std::mutex> lock(imp_->queue_mutex_);
        if (imp_->config_.max_queue_size > 0) {
            // 队列满时等待
            imp_->not_full_condition_.wait(lock, [this](){
                return imp_->stop_ || imp_->tasks_.size() < imp_->config_.max_queue_size;
            });
        }
        if (imp_->stop_) {
            return false;
        }
        imp_->tasks_.emplace(std::move(func), priority);

        std::string priority_str = (priority == TaskPriority::HIGH) ? "HIGH"
                                : (priority == TaskPriority::NORMAL) ? "NORMAL"
                                : "LOW";
        LOG_INFO("[ThreadPool] Enqueuing task with priority: {}, queue: {}/{}, active: {}/{}",
                 priority_str, imp_->tasks_.size(), imp_->config_.max_queue_size,
                 imp_->active_threads_.load(), imp_->current_threads_.load());

        if (imp_->tasks_.size() > imp_->active_threads_ && imp_->current_threads_ < imp_->config_.max_threads) {
            AdjustThreadCount_();
        }
    }
    imp_->condition_.notify_one();
    return true;
}

} // namespace meeting_ctrl
