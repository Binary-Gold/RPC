#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <zookeeper/zookeeper.h>

#include "load_balancer/zk_conn_handler.hpp"
#include "log_manager.hpp"
#include "registry/services_registry.hpp"
#include "load_balancer/load_balancer.hpp"

namespace cookrpc {

struct ZkConnHandler::Imp {
    std::atomic<bool> running_{false};
    std::chrono::seconds retry_interval_;
    std::mutex mtx_;
    std::mutex servers_mtx_;
    std::vector<std::string> servers_;
    zhandle_t *zk_client_{nullptr};
    std::string zk_host_;
    int zk_port_;
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
        } catch (const nlohmann::json::exception &e) {
            LOG_ERROR("Fail to parse config : {}", e.what());
            Cleanup();
            return false;
        }
        bool is_connected = EnsureZkConnection();
        if (!imp_->zk_client_) {
            LOG_ERROR("Failed to initialize ZooKeeper client: {}", strerror(errno));
            return false;
        }
        if (!is_connected) {
            LOG_ERROR("Failed to connect ZooKeeper after retries");
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("Exception in initZkConnHandler : {}", e.what());
        Cleanup();
        return false;
    }
}

std::vector<std::string> ZkConnHandler::GetAllServices(const std::string& zk_namespace) {
    if (!EnsureZkConnection()) {
        LOG_ERROR("ZooKeeper connection not available");
        return {};
    }

    std::lock_guard<std::mutex> lock(imp_->servers_mtx_);
    try {
        struct String_vector children = {0};  

        int rc = zoo_get_children(imp_->zk_client_, zk_namespace.c_str(), 0, &children);

        if (rc != ZOK) {
            LOG_ERROR("Failed to get children: {}", zerror(rc));
            return {};
        }
        
        std::vector<std::string> servers;
        
        for (int i = 0; i < children.count; i++) {
            std::string node_path = zk_namespace + "/" + children.data[i];
            char buffer[1024];
            int buffer_len = sizeof(buffer);

            int ret = zoo_get(imp_->zk_client_, node_path.c_str(), 0, buffer, &buffer_len, nullptr);
            if (ret == ZOK && buffer_len > 0) {
                std::string server_ip(buffer, buffer_len);
                servers.push_back(server_ip);
            } else {
                LOG_WARN("Failed to get data for node: {}, error: {}", node_path, zerror(ret));
            }
        }
        deallocate_String_vector(&children);

        return servers;
    } catch (const std::exception &e) {
        LOG_ERROR("Exception in getAllServers: {}", e.what());
        return {};  // 异常时返回空列表
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
            // // LOG_INFO("Successfully registered service: {} at {}", service_name, service_address);
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
            if (!CreateServiceRegistryIfNeeded_()) {
                return nullptr;
            }
        }
    }
    return imp_->service_registry_.get();
}
bool ZkConnHandler::CreateServiceRegistryIfNeeded_() {
    try {
        std::string zk_connection = imp_->zk_host_ + ":" + std::to_string(imp_->zk_port_);
        // // LOG_INFO("Creating ServiceRegistry with connection: {}", zk_connection);

        imp_->service_registry_ = std::make_unique<ServiceRegistry>(zk_connection);
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!imp_->service_registry_->IsConnected()) {
            LOG_ERROR("Failed to connect ServiceRegistry to ZooKeeper at {}", zk_connection);
            imp_->service_registry_.reset();
            return false;
        }
        
        // // LOG_INFO("ServiceRegistry connected successfully to ZooKeeper at {}", zk_connection);
        return true;
    
    } catch (const std::exception& e) {
        LOG_ERROR("Exception creating ServiceRegistry: {}", e.what());
        imp_->service_registry_.reset();
        return false;
    }
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
    static std::atomic<bool> cleanup_done{false};
    if (cleanup_done.exchange(true)) {
        return;
    }
    
    imp_->service_registry_.reset();
    
    if (imp_->zk_client_) {
        try {
            zookeeper_close(imp_->zk_client_);
            imp_->zk_client_ = nullptr;
        } catch (...) {
            imp_->zk_client_ = nullptr;
        }
    }
    imp_->running_ = false;
}

void ZkConnHandler::UpdateServersFromZk(const std::string& zk_namespace) {
    auto new_servers = GetAllServices(zk_namespace);
    {
        std::lock_guard<std::mutex> lock(imp_->servers_mtx_);
        imp_->servers_ = std::move(new_servers);
    }
}

void ZkConnHandler::GlobalWatcher_(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {
        const char *state_desc;
        if (state == ZOO_CONNECTED_STATE) {
            state_desc = "CONNECTED";
        } else if (state == ZOO_CONNECTING_STATE) {
            state_desc = "CONNECTING";
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            state_desc = "EXPIRED";
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            state_desc = "AUTH_FAILED";
        } else if (state == ZOO_ASSOCIATING_STATE) {
            state_desc = "ASSOCIATING";
        } else {
            state_desc = "UNKNOWN";
        }
        LOG_DEBUG("ZooKeeper watcher event: {} (type={})", state_desc, type);
    }
}

bool ZkConnHandler::EnsureZkConnection() {
    if (!imp_->zk_client_) {
        std::string conn_string = imp_->zk_host_ + ":" + std::to_string(imp_->zk_port_); 

        // 初始化ZooKeeper客户端
        imp_->zk_client_ = zookeeper_init(conn_string.c_str(),  // 连接字符串
                                        GlobalWatcher_,       // 全局监听器
                                        30000,               // 30秒超时
                                        nullptr,             // 客户端ID（新会话）
                                        this,                // 监听器上下文
                                        0                    // 标志位
                                    );

        if (!imp_->zk_client_) {
            LOG_ERROR("Failed to initialize ZooKeeper client: {} (errno: {})", strerror(errno), errno);
            return false;
        }
    }

    int retry_count = 0;
    const int max_retries = 50;
    
    while (retry_count++ < max_retries) {
        int state = zoo_state(imp_->zk_client_);
        const char *state_desc;

        if (state == ZOO_CONNECTED_STATE) {
            state_desc = "CONNECTED";
        } else if (state == ZOO_CONNECTING_STATE) {
            state_desc = "CONNECTING";
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            state_desc = "EXPIRED";
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            state_desc = "AUTH_FAILED";
        } else if (state == ZOO_ASSOCIATING_STATE) {
            state_desc = "ASSOCIATING";
        } else {
            state_desc = "UNKNOWN";
            LOG_WARN("Unexpected state value: {} (0x{:x})", state, state);
            LOG_WARN("Client handle valid: {}", (imp_->zk_client_ != nullptr));
            
            if (imp_->zk_client_) {
                struct Stat stat = {0};
                int rc = zoo_exists(imp_->zk_client_, "/", 0, &stat);
                LOG_WARN("zoo_exists test result: {}", rc);
            }
        }

        LOG_DEBUG("ZooKeeper state: {} (retry {}/{})", state_desc, retry_count, max_retries);

        if (state == ZOO_CONNECTED_STATE) {
            return true;
        } else if (state == ZOO_EXPIRED_SESSION_STATE ||
                    state == ZOO_AUTH_FAILED_STATE ||
                    (state != ZOO_CONNECTING_STATE && retry_count > 10)) {
            LOG_ERROR("Connection failed with state: {} ({})", state, state_desc);
            Cleanup();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_ERROR("Failed to connect after {} retries", max_retries);
    Cleanup();
    return false;
}

} // namespace cookrpc
