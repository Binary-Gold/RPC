#include <fstream>
#include <nlohmann/json.hpp>

#include "log_manager.hpp"
#include "load_config/rpc_client_config.hpp"

namespace cookrpc
{

struct RpcClientConfig::Imp {
    std::string zk_namespace_;
    std::string zk_host_;
    int zk_port_{0};
    int timeout_ms_{3000};
    int retry_times_{3};
    int server_port_{0};
};

RpcClientConfig::RpcClientConfig() : imp_(std::make_unique<Imp>()) {}
RpcClientConfig::~RpcClientConfig() = default;

RpcClientConfig& RpcClientConfig::GetInstance() {
    static RpcClientConfig instance;
    return instance;
}

bool RpcClientConfig::InitRpcClientConfig(const std::string& config_filename) {
    std::ifstream file(config_filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file: {}", config_filename);
        return false;
    }
    nlohmann::json config = nlohmann::json::parse(file);
    SetZkNamespace(config.value("zk_namespace", "cookrpc"));
    SetZkHost(config.value("zk_host", "localhost"));
    SetZkPort(config.value("zk_port", 2181));
    SetTimeout(config.value("timeout_ms", 3000));
    SetRetryTimes(config.value("retry_times", 3));
    SetServerPort(config.value("server_port", 8989));
    return true;
}

void RpcClientConfig::SetZkNamespace(const std::string& zk_namespace) { imp_->zk_namespace_ = zk_namespace; }
void RpcClientConfig::SetZkHost(const std::string& zk_host)           { imp_->zk_host_ = zk_host; }
void RpcClientConfig::SetZkPort(int zk_port)                          { imp_->zk_port_ = zk_port; }
void RpcClientConfig::SetTimeout(int timeout_ms)                      { imp_->timeout_ms_ = timeout_ms; }
void RpcClientConfig::SetRetryTimes(int retry_times)                  { imp_->retry_times_ = retry_times; }
void RpcClientConfig::SetServerPort(int server_port)                  { imp_->server_port_ = server_port; }

int RpcClientConfig::GetTimeout() const                { return imp_->timeout_ms_; }
int RpcClientConfig::GetRetryTimes() const              { return imp_->retry_times_; }
const std::string& RpcClientConfig::GetZkNamespace() const { return imp_->zk_namespace_; }
int RpcClientConfig::GetServerPort() const              { return imp_->server_port_; }
const std::string& RpcClientConfig::GetZkHost() const   { return imp_->zk_host_; }
int RpcClientConfig::GetZkPort() const                  { return imp_->zk_port_; }

} // namespace cookrpc
