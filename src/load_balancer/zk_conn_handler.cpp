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

ZkConnHandler::~ZkConnHandler() = default;

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
            LOG_ERROR("Failed to initalize ZooKeeper client : {}", strerror(errno));
            return false;
        }
        if (!is_connected) {
            LOG_ERROR("Failed to connected ZooKeeper after retries");
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
    return {};
}

std::string ZkConnHandler::GetServices(const std::string& zk_namespace) {
    return {};
}

bool ZkConnHandler::RegisterService(const std::string& service_name, const std::string& service_address) {
    return false;
}

bool ZkConnHandler::RegisterServicesFromConfig(const ServiceRegistryConfig* config) {
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
    return nullptr;
}

void ZkConnHandler::SetZkHost(const std::string& host) {
    std::lock_guard<std::mutex> lock(imp_->mtx_);
    try
    {
        if (host.empty())
        {
            LOG_WARN("Empty host provided, using default");
            imp_->zk_host_ = "localhost";
        }
        else
        {
            imp_->zk_host_ = host;
        }
    }
    catch (const std::exception &e)
    {
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
}

void ZkConnHandler::UpdateServersFromZk(const std::string& zk_namespace) {
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
    if (!imp_->zk_client_)
    {
        std::string conn_string = imp_->zk_host_ + ":" + std::to_string(imp_->zk_port_); 

        // 初始化ZooKeeper客户端
        imp_->zk_client_ = zookeeper_init(conn_string.c_str(),  // 连接字符串
                                        GlobalWatcher_,       // 全局监听器
                                        30000,               // 30秒超时
                                        nullptr,             // 客户端ID（新会话）
                                        this,                // 监听器上下文
                                        0                    // 标志位
                                    );

        if (!imp_->zk_client_)
        {
            LOG_ERROR("Failed to initialize ZooKeeper client: {} (errno: {})",
                        strerror(errno), errno);
            return false;
        }
    }

    int retry_count = 0;
    const int max_retries = 50;
    
    while (retry_count++ < max_retries)
    {
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
            
            if (imp_->zk_client_)
            {
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
