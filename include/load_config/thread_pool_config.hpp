#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

namespace cookrpc
{

    class ThreadPoolConfig
    {
    public:
        ThreadPoolConfig();
        ~ThreadPoolConfig();

        bool InitThreadPoolConfig(const nlohmann::json& config);

        uint16_t GetMaxThreads() const;
        uint32_t GetQueueSize() const;
        uint32_t GetKeepAliveTime() const;
        uint16_t GetCoreThreads() const;

        void SetMaxThreads(uint16_t max_threads);
        void SetQueueSize(uint32_t queue_size);
        void SetKeepAliveTime(uint32_t keep_alive_time);
        void SetCoreThreads(uint16_t core_threads);

        ThreadPoolConfig(const ThreadPoolConfig&) = delete;
        ThreadPoolConfig& operator=(const ThreadPoolConfig&) = delete;
        ThreadPoolConfig(ThreadPoolConfig&&) = delete;
        ThreadPoolConfig& operator=(ThreadPoolConfig&&) = delete;

    private:
        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

}
