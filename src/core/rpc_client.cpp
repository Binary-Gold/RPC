#include "core/rpc_client.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "log_manager.hpp"
#include "load_balancer/zk_conn_handler.hpp"
#include "load_config/rpc_client_config.hpp"
#include "compress_data/zstd_compress.hpp"
#include "encrypt/aes_encrypt.hpp"

namespace cookrpc
{

struct RpcClient::Imp {
    int timeout_ms_{3000};
    int retry_times_{3};
    std::string zk_namespace_;
    int socket_fd_{-1};
    std::shared_ptr<Connection> connection_;
    std::mutex mutex_;
    std::atomic<uint64_t> sequence_id_{0};
    std::atomic<bool> is_connected_{false};
};

RpcClient::RpcClient(const std::string& config_path)
    : imp_(std::make_unique<Imp>())
{
    try {
        if (!loadConfig(config_path)) {
            throw std::runtime_error("Failed to load RPC client config");
        }
        if (!initSocket(imp_->socket_fd_)) {
            throw std::runtime_error("Failed to initialize socket");
        }
        if (!Connect()) {
            throw std::runtime_error("Failed to connect to RPC server");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("RPC client initialization failed: {}", e.what());
        Disconnect();
        throw;
    }
}

RpcClient::~RpcClient() {
    Disconnect();
}

bool RpcClient::IsConnected() const {
    return imp_->is_connected_;
}

std::mutex& RpcClient::GetMutex() {
    return imp_->mutex_;
}

std::shared_ptr<Connection>& RpcClient::GetConnection() {
    return imp_->connection_;
}

int RpcClient::GetTimeoutMs() const {
    return imp_->timeout_ms_;
}

bool RpcClient::initSocket(int& fd) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Create socket failed: {}", strerror(errno));
        return false;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Set nonblock failed: {}", strerror(errno));
        close(fd);
        return false;
    }
    return true;
}

bool RpcClient::loadConfig(const std::string& conf_path) {
    auto& rpc_client_config = RpcClientConfig::GetInstance();
    if (!rpc_client_config.InitRpcClientConfig(conf_path)) {
        LOG_ERROR("load config failed: {}", conf_path);
        return false;
    }
    imp_->retry_times_ = rpc_client_config.GetRetryTimes();
    imp_->timeout_ms_ = rpc_client_config.GetTimeout();
    imp_->zk_namespace_ = rpc_client_config.GetZkNamespace();

    auto& zk_conn_handler = ZkConnHandler::GetInstance();
    nlohmann::json zk_config;
    zk_config["zk_host"] = rpc_client_config.GetZkHost();
    zk_config["zk_port"] = rpc_client_config.GetZkPort();
    zk_config["zk_namespace"] = rpc_client_config.GetZkNamespace();
    zk_config["zk_retry_interval"] = 5;

    if (!zk_conn_handler.InitZkConnHandler(zk_config)) {
        LOG_ERROR("Failed to initialize ZkConnHandler");
        return false;
    }
    return true;
}

bool RpcClient::validateServerInfo(const std::string& ip, int port) {
    if (ip.empty()) {
        LOG_ERROR("Empty IP address");
        return false;
    }
    struct sockaddr_in sa;
    if (inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 1) {
        LOG_ERROR("Invalid IP address format: {}", ip);
        return false;
    }
    if (port <= 0 || port > 65535) {
        LOG_ERROR("Invalid port number: {}", port);
        return false;
    }
    return true;
}

bool RpcClient::tryConnect(int socket_fd, const struct sockaddr_in& server_addr, int retry_count) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == 0) {
        fcntl(socket_fd, F_SETFL, flags);
        return true;
    }

    if (errno != EINPROGRESS && errno != EALREADY) {
        LOG_ERROR("Connect failed (attempt {}/{}): {} (errno: {})",
                 retry_count + 1, imp_->retry_times_, strerror(errno), errno);
        return false;
    }

    fd_set write_fds;
    struct timeval timeout;
    timeout.tv_sec = imp_->timeout_ms_ / 1000;
    timeout.tv_usec = (imp_->timeout_ms_ % 1000) * 1000;
    FD_ZERO(&write_fds);
    FD_SET(socket_fd, &write_fds);

    ret = select(socket_fd + 1, nullptr, &write_fds, nullptr, &timeout);
    if (ret <= 0) {
        LOG_ERROR("Connect timeout or error (attempt {}/{}): {}",
                 retry_count + 1, imp_->retry_times_, ret == 0 ? "timeout" : strerror(errno));
        return false;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        LOG_ERROR("Connection failed after select (attempt {}/{}): {}",
                 retry_count + 1, imp_->retry_times_, error == 0 ? "getsockopt failed" : strerror(error));
        return false;
    }

    fcntl(socket_fd, F_SETFL, flags);
    return true;
}

bool RpcClient::Connect() {
    std::lock_guard<std::mutex> lock(imp_->mutex_);
    if (imp_->is_connected_) {
        return true;
    }

    std::string server_ip_and_port = ZkConnHandler::GetInstance().GetServices(imp_->zk_namespace_);
    if (server_ip_and_port.empty()) {
        LOG_ERROR("fail to get server_ip");
        return false;
    }

    std::string server_ip = server_ip_and_port.substr(0, server_ip_and_port.find(":"));
    int server_port = std::stoi(server_ip_and_port.substr(server_ip_and_port.find(":") + 1));

    if (!validateServerInfo(server_ip, server_port)) {
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    server_addr.sin_port = htons(server_port);

    int retry_count = 0;
    while (retry_count < imp_->retry_times_) {
        int socket_fd;
        if (!initSocket(socket_fd)) {
            LOG_ERROR("Init socket failed");
            retry_count++;
            continue;
        }

        if (tryConnect(socket_fd, server_addr, retry_count)) {
            imp_->connection_ = std::make_shared<Connection>(socket_fd);
            imp_->is_connected_ = true;
            return true;
        }

        close(socket_fd);
        retry_count++;
    }

    return false;
}

void RpcClient::Disconnect() {
    std::lock_guard<std::mutex> lock(imp_->mutex_);
    if (imp_->connection_) {
        imp_->connection_->Close();
        imp_->connection_.reset();
    }
    imp_->is_connected_ = false;
}

bool RpcClient::Reconnect() {
    Disconnect();
    return Connect();
}

uint64_t RpcClient::GenerateSequenceId() {
    return ++imp_->sequence_id_;
}

bool RpcClient::checkConnection() {
    if (!IsConnected() && !Reconnect()) {
        LOG_ERROR("Not connected to server");
        return false;
    }
    return true;
}

bool RpcClient::sendAndReceiveResponse(const std::string& encrypted_data) {
    if (!imp_->connection_->Write(encrypted_data)) {
        LOG_ERROR("Failed to send request");
        imp_->is_connected_ = false;
        return false;
    }
    return true;
}

std::string RpcClient::GetServerAddress(const std::string& zk_namespace) const {
    return {};
}

} // namespace cookrpc
