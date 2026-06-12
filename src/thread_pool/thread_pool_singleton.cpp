#include "thread_pool/thread_pool_singleton.hpp"

namespace meeting_ctrl {

std::unique_ptr<ThreadPool> ThreadPoolSingleton::instance_;
std::mutex ThreadPoolSingleton::mutex_;

bool ThreadPoolSingleton::Init(size_t threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_) {
        return false;
    }
    instance_ = std::make_unique<ThreadPool>(threads);
    return true;
}

bool ThreadPoolSingleton::Init(const ThreadPoolStruct& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_) {
        return false;
    }
    instance_ = std::make_unique<ThreadPool>(config);
    return true;
}

ThreadPool& ThreadPoolSingleton::GetInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        instance_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
    }
    return *instance_;
}

ThreadPool::Stats ThreadPoolSingleton::GetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        return ThreadPool::Stats{};
    }
    return instance_->GetStats();
}

size_t ThreadPoolSingleton::GetQueueSize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        return 0;
    }
    return instance_->QueueSize();
}

size_t ThreadPoolSingleton::GetPoolSize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        return 0;
    }
    return instance_->Size();
}

bool ThreadPoolSingleton::Shutdown(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        return true;
    }
    bool success = instance_->Shutdown(timeout);
    if (success) {
        instance_.reset();
    }
    return success;
}

void ThreadPoolSingleton::Destroy() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_) {
        instance_->StopNow();
        instance_.reset();
    }
}

bool ThreadPoolSingleton::Exists() {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<bool>(instance_);
}

ThreadPoolState ThreadPoolSingleton::GetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!instance_) {
        return ThreadPoolState::STOPPED;
    }
    return instance_->GetState();
}

} // namespace meeting_ctrl
