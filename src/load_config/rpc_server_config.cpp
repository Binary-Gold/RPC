#include <fstream>
#include <nlohmann/json.hpp>

#include "log_manager.hpp"
#include "load_config/rpc_server_config.hpp"
#include "load_config/thread_pool_config.hpp"
#include "load_config/registry_config.hpp"
#include "load_balancer/zk_conn_handler.hpp"
#include "load_balancer/load_balancer.hpp"

namespace cookrpc
{

struct RpcServerConfig::Imp {
    uint16_t max_connections_{0};
    std::string servers_ip_;
    uint16_t servers_port_{0};
    uint32_t timeout_{0};
    std::string servers_name_prefix_;
    std::unique_ptr<ThreadPoolConfig> thread_pool_config_;
    std::unique_ptr<ServiceRegistryConfig> service_registry_config_;
};

RpcServerConfig::RpcServerConfig()
    : imp_(std::make_unique<Imp>()) {
    imp_->thread_pool_config_ = std::make_unique<ThreadPoolConfig>();
    imp_->service_registry_config_ = std::make_unique<ServiceRegistryConfig>();
}

RpcServerConfig::~RpcServerConfig() = default;

RpcServerConfig& RpcServerConfig::GetInstance() {
    static RpcServerConfig instance;
    return instance;
}

bool RpcServerConfig::InitRpcServerConfig(const std::string& config_file) {
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open config file: {}", config_file);
            return false;
        }

        nlohmann::json config = nlohmann::json::parse(file);

        SetServersNamePrefix(config.value("servers_name_prefix", "cookrpc"));
        SetServersIp(config.value("servers_ip", "0.0.0.0"));
        SetServersPort(config.value("servers_port", 8989));
        SetTimeout(config.value("timeout", 3000));
        SetMaxConnections(config.value("max_connections", 1000));

        if (config.contains("register_config")) {
            std::string register_config_file = config.value("register_config", "../config/servers.json");
            if (!imp_->service_registry_config_->InitRegistryConfig(register_config_file)) {
                LOG_ERROR("Failed to initialize registry config");
                return false;
            }
        }

        if (config.contains("thread_pool")) {
            if (!imp_->thread_pool_config_->InitThreadPoolConfig(config["thread_pool"])) {
                LOG_ERROR("Failed to initialize thread pool config");
                return false;
            }
        }

        if (config.contains("scheduler")) {
            auto& zk_conn_handler = ZkConnHandler::GetInstance();
            if (!zk_conn_handler.InitZkConnHandler(config["scheduler"])) {
                LOG_ERROR("Failed to initialize zk conn handler");
                return false;
            }
        }

        if (config.contains("load_balance_strategy")) {
            std::string strategy = config["load_balance_strategy"];
            if (!LoadBalancer::initBalancer(strategy)) {
                LOG_ERROR("Failed to initialize load balancer with strategy: {}", strategy);
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error initializing config: {}", e.what());
        return false;
    }
}

void RpcServerConfig::SetServersNamePrefix(const std::string& v) { imp_->servers_name_prefix_ = v; }
void RpcServerConfig::SetServersIp(const std::string& v)         { imp_->servers_ip_ = v; }
void RpcServerConfig::SetServersPort(uint16_t v)                  { imp_->servers_port_ = v; }
void RpcServerConfig::SetTimeout(uint32_t v)                      { imp_->timeout_ = v; }
void RpcServerConfig::SetMaxConnections(uint16_t v)               { imp_->max_connections_ = v; }

const std::string& RpcServerConfig::GetServersNamePrefix() const { return imp_->servers_name_prefix_; }
const std::string& RpcServerConfig::GetServersIp() const         { return imp_->servers_ip_; }
uint16_t RpcServerConfig::GetServersPort() const                  { return imp_->servers_port_; }
int RpcServerConfig::GetTimeout() const                           { return imp_->timeout_; }
int RpcServerConfig::GetMaxConnections() const                    { return imp_->max_connections_; }

const ServiceRegistryConfig* RpcServerConfig::GetServiceRegistryConfig() const {
    return imp_->service_registry_config_.get();
}
const ThreadPoolConfig* RpcServerConfig::GetThreadPoolConfig() const {
    return imp_->thread_pool_config_.get();
}

} // namespace cookrpc
