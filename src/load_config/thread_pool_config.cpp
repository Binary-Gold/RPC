#include "log_manager.hpp"
#include "load_config/thread_pool_config.hpp"

namespace cookrpc
{

struct ThreadPoolConfig::Imp {
    uint16_t max_threads_{0};
    uint32_t queue_size_{0};
    uint32_t keep_alive_time_{0};
    uint16_t core_threads_{0};
};

ThreadPoolConfig::ThreadPoolConfig() : imp_(std::make_unique<Imp>()) {}
ThreadPoolConfig::~ThreadPoolConfig() = default;

bool ThreadPoolConfig::InitThreadPoolConfig(const nlohmann::json& config) {
    try {
        uint16_t max_threads = config.value("max_threads", 10);
        if (max_threads == 0) {
            LOG_ERROR("Invalid max_threads");
            return false;
        }
        SetMaxThreads(max_threads);

        uint32_t queue_size = config.value("queue_size", 100);
        if (queue_size == 0) {
            LOG_ERROR("Invalid queue_size");
            return false;
        }
        SetQueueSize(queue_size);

        uint32_t keep_alive_time = config.value("keep_alive_time", 60);
        if (keep_alive_time == 0) {
            LOG_ERROR("Invalid keep_alive_time");
            return false;
        }
        SetKeepAliveTime(keep_alive_time);

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to init thread pool config: {}", e.what());
        return false;
    }
}

void   ThreadPoolConfig::SetMaxThreads(uint16_t v)    { imp_->max_threads_ = v; }
void   ThreadPoolConfig::SetQueueSize(uint32_t v)      { imp_->queue_size_ = v; }
void   ThreadPoolConfig::SetKeepAliveTime(uint32_t v)  { imp_->keep_alive_time_ = v; }
void   ThreadPoolConfig::SetCoreThreads(uint16_t v)    { imp_->core_threads_ = v; }

uint16_t ThreadPoolConfig::GetMaxThreads() const    { return imp_->max_threads_; }
uint32_t ThreadPoolConfig::GetQueueSize() const      { return imp_->queue_size_; }
uint32_t ThreadPoolConfig::GetKeepAliveTime() const  { return imp_->keep_alive_time_; }
uint16_t ThreadPoolConfig::GetCoreThreads() const    { return imp_->core_threads_; }

} // namespace cookrpc
