#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "load_balancer/zk_conn_handler.hpp"
#include "load_balancer/load_balancer.hpp"
#include "log_manager.hpp"
#include "registry/services_registry.hpp"

namespace cookrpc {

struct ZkConnHandler::Imp {
    std::atomic<bool> running_{false};
    std::atomic<bool> cleanup_done_{false};
    std::chrono::seconds retry_interval_;
    std::mutex mtx_;
    std::mutex servers_mtx_;
    std::vector<std::string> servers_;
    std::string zk_host_;
    int zk_port_{2181};
    std::string zk_namespace_;
    std::unique_ptr<ServiceRegistry> service_registry_{nullptr};
};

ZkConnHandler::ZkConnHandler() : imp_(std::make_unique<Imp>()) {}

ZkConnHandler::~ZkConnHandler() {
    Cleanup();
}

bool ZkConnHandler::InitZkConnHandler(const nlohmann::json& config) {
    try {
        if (config.empty()) {
            LOG_ERROR("Empty config");
            return false;
        }
        try {
            SetZkHost(config.value("zk_host", "localhost"));
            SetZkPort(config.value("zk_port", 2181));
            SetZkNamespace(config.value("zk_namespace", "/cookrpc"));
            SetZkRetryInterval(config.value("zk_retry_interval", 5));
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("Fail to parse config: {}", e.what());
            return false;
        }

        ServiceRegistry* registry = GetOrCreateServiceRegistry();
        if (!registry) {
            LOG_ERROR("Failed to create ServiceRegistry");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in InitZkConnHandler: {}", e.what());
        Cleanup();
        return false;
    }
}

std::vector<std::string> ZkConnHandler::GetAllServices(const std::string& zk_namespace) {
    if (!imp_->service_registry_) {
        LOG_ERROR("ServiceRegistry not available");
        return {};
    }

    std::lock_guard<std::mutex> lock(imp_->servers_mtx_);
    try {
        return imp_->service_registry_->DiscoverServices(zk_namespace);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in GetAllServices: {}", e.what());
        return {};
    }
}

std::string ZkConnHandler::GetServices(const std::string& zk_namespace) {
    UpdateServersFromZk(zk_namespace);
    std::lock_guard<std::mutex> lock(imp_->servers_mtx_);

    if (imp_->servers_.empty()) {
        LOG_ERROR("No available servers");
        return {};
    }

    std::string server = LoadBalancer::selectServer(imp_->servers_);
    return server;
}

bool ZkConnHandler::RegisterService(const std::string& service_name, const std::string& service_address) {
    try {
        ServiceRegistry* registry = GetOrCreateServiceRegistry();
        if (!registry) {
            LOG_ERROR("Failed to get or create ServiceRegistry for service registration");
            return false;
        }

        if (registry->RegisterService(service_name, service_address)) {
            return true;
        } else {
            LOG_ERROR("Failed to register service: {} at {}", service_name, service_address);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during service registration: {}", e.what());
        return false;
    }
}

bool ZkConnHandler::RegisterServicesFromConfig(const ServiceRegistryConfig* registry_config) {
    // todo registry_config->GetRegistryNodesSize()
    // if (!registry_config || registry_config->GetRegistryNodesSize() == 0)
    // {
    //     LOG_WARN("No registry configuration found, skipping ZooKeeper registration");
    //     return true; 
    // }

    // try 
    // {
    //     ServiceRegistry* registry = GetOrCreateServiceRegistry();
    //     if (!registry) {
    //         LOG_ERROR("Failed to get or create ServiceRegistry for batch service registration");
    //         return false;
    //     }
        
    //     // 注册配置文件中的所有服务节点
    //     const auto& registry_nodes = registry_config->GetRegistryNodes();
    //     const std::string& service_name = registry_config->GetServiceName();
        
    //     bool all_success = true;
    //     for (const auto& node : registry_nodes)
    //     {
    //         std::string service_address = node.address + ":" + std::to_string(node.port);
    //         if (registry->registerService(service_name, service_address))
    //         {
    //             // // LOG_INFO("Successfully registered service: {} at {}", service_name, service_address);
    //         }
    //         else
    //         {
    //             // LOG_ERROR("Failed to register service: {} at {}", service_name, service_address);
    //             all_success = false;  // 记录失败但继续处理其他节点
    //         }
    //     }
        
    //     // 重要：不在这里销毁service_registry_，保持连接以维持临时节点
    //     // ZooKeeper的临时节点在连接断开时会自动删除，这是服务发现的重要机制
    //     // 智能指针会在对象销毁时自动管理内存
    //     // // LOG_INFO("Service registration completed. Keeping ZooKeeper connection alive to maintain ephemeral nodes.");
        
    //     return all_success;
    // }
    // catch (const std::exception& e)
    // {
    //     LOG_ERROR("Exception during service registration: {}", e.what());
    //     return false;
    // }

    return false;
}

ServiceRegistry* ZkConnHandler::GetServiceRegistry() {
    return imp_->service_registry_.get();
}

const ServiceRegistry* ZkConnHandler::GetServiceRegistry() const {
    return imp_->service_registry_.get();
}

bool ZkConnHandler::HasServiceRegistry() const {
    return imp_->service_registry_ != nullptr;
}

ServiceRegistry* ZkConnHandler::GetOrCreateServiceRegistry() {
    if (!imp_->service_registry_) {
        std::lock_guard<std::mutex> lock(imp_->mtx_);
        if (!imp_->service_registry_) {
            try {
                std::string zk_connection = imp_->zk_host_ + ":" + std::to_string(imp_->zk_port_);
                imp_->service_registry_ = std::make_unique<ServiceRegistry>(zk_connection);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception creating ServiceRegistry: {}", e.what());
                imp_->service_registry_.reset();
                return nullptr;
            }
        }
    }
    return imp_->service_registry_.get();
}

void ZkConnHandler::SetZkHost(const std::string& host) {
    std::lock_guard<std::mutex> lock(imp_->mtx_);
    try {
        if (host.empty()) {
            LOG_WARN("Empty host provided, using default");
            imp_->zk_host_ = "localhost";
        } else {
            imp_->zk_host_ = host;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to set ZooKeeper host: {}", e.what());
        imp_->zk_host_ = "localhost";
    }
}

void ZkConnHandler::SetZkPort(int port) {
    std::lock_guard<std::mutex> lock(imp_->mtx_);
    imp_->zk_port_ = (port > 0 && port < 65536) ? port : 2181;
}

void ZkConnHandler::SetZkRetryInterval(int seconds) {
    imp_->retry_interval_ = std::chrono::seconds(seconds);
}

void ZkConnHandler::SetZkNamespace(const std::string& zk_namespace) {
    imp_->zk_namespace_ = zk_namespace;
}

void ZkConnHandler::Cleanup() {
    if (imp_->cleanup_done_.exchange(true)) {
        return;
    }

    imp_->service_registry_.reset();
    imp_->running_ = false;
}

void ZkConnHandler::ResetForTesting() {
    imp_->cleanup_done_ = false;
    imp_->service_registry_.reset();
}

void ZkConnHandler::UpdateServersFromZk(const std::string& zk_namespace) {
    auto new_servers = GetAllServices(zk_namespace);
    {
        std::lock_guard<std::mutex> lock(imp_->servers_mtx_);
        imp_->servers_ = std::move(new_servers);
    }
}

} // namespace cookrpc
