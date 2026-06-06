#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <future>

#include "service.hpp"
#include "log_manager.hpp"
#include "thread_pool/thread_pool_singleton.hpp"

namespace cookrpc
{
    class ServiceManager
    {
    public:
        static ServiceManager &GetInstance() {
            static ServiceManager instance;
            return instance;
        }

        // 注册服务
        bool RegisterService(std::shared_ptr<Service> service) {
            if (!service) {
                LOG_ERROR("register null service");
                return false;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            std::string service_name = service->GetServiceName();

            if (services_.find(service_name) != services_.end()) {
                LOG_WARN("service already exists: {}", service_name);
                return false;
            }

            services_[service_name] = service;

            return true;
        }

        // 获取服务
        std::shared_ptr<Service> GetService(const std::string &service_name) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = services_.find(service_name);
            if (it != services_.end()) {
                return it->second;
            }
            LOG_WARN("service not found get service: {}", service_name);
            return nullptr;
        }

        // 异步处理RPC请求 - 使用线程池
        std::future<bool> HandleRpcRequestAsync(const std::string &service_name,
                                               const std::string &method_name,
                                               const std::string &args,
                                               std::shared_ptr<std::string> result) {
            if (!meeting_ctrl::ThreadPoolSingleton::Exists()) {
                meeting_ctrl::ThreadPoolSingleton::Init();
            }

            // 使用高优先级提交RPC请求处理任务
            return meeting_ctrl::ThreadPoolSingleton::GetInstance().Enqueue(
                meeting_ctrl::TaskPriority::HIGH,
                [this, service_name, method_name, args, result]() -> bool {
                    return this->HandleRpcRequestSync_(service_name, method_name, args, *result);
                }
            );
        }

        // 同步处理RPC请求
        bool HandleRpcRequest(const std::string &service_name,
                              const std::string &method_name,
                              const std::string &args,
                              std::string &result) {
            return HandleRpcRequestSync_(service_name, method_name, args, result);
        }

        // 获取线程池统计信息
        meeting_ctrl::ThreadPool::Stats GetThreadPoolStats() const
        {
            if (meeting_ctrl::ThreadPoolSingleton::Exists()) {
                return meeting_ctrl::ThreadPoolSingleton::GetInstance().GetStats();
            }
            return meeting_ctrl::ThreadPool::Stats{};
        }

    private:
        ServiceManager() = default;

        ServiceManager(const ServiceManager &) = delete;
        ServiceManager &operator=(const ServiceManager &) = delete;

        bool HandleRpcRequestSync_(const std::string &service_name,
                                 const std::string &method_name,
                                 const std::string &args,
                                 std::string &result) {
            auto service = GetService(service_name);
            if (!service) {
                LOG_ERROR("service not found handle rpc request: {}", service_name);
                return false;
            }

            if (!service->HandleRequest(method_name, args, result)) {
                LOG_ERROR("handle request failed: service={}, method={}",
                          service_name, method_name);
                return false;
            }

            return true;
        }

        std::unordered_map<std::string, std::shared_ptr<Service>> services_;

        mutable std::mutex mutex_;
    };

} // namespace cookrpc
