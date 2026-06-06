#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "network/create_socket.hpp"

namespace cookrpc {

struct CreateSocket::Imp {
    int fd_{-1};
    std::string servers_name_prefix_;
    std::string servers_ip_;
    uint16_t servers_port_;
    int servers_max_connections_;
    int socket_timeout_ms_;
};

CreateSocket::CreateSocket(const std::string& servers_name_prefix,
                            uint16_t servers_port,
                            int servers_max_connections,
                            int socket_timeout_ms,
                            const std::string& servers_ip)
                            : imp_(std::make_unique<Imp>()) {
    imp_->servers_name_prefix_= servers_name_prefix;
    imp_->servers_ip_ = servers_ip;
    imp_->servers_port_ = servers_port;
    imp_->servers_max_connections_ = servers_max_connections;
    imp_->socket_timeout_ms_ = socket_timeout_ms;
}

CreateSocket::~CreateSocket() {
    if (imp_->fd_ > 0) {
        ::close(imp_->fd_);
        imp_->fd_ = -1;
    }
}

std::shared_ptr<CreateSocket> CreateSocket::Create(
    const std::string& servers_name_prefix,
    uint16_t servers_port,
    int servers_max_connections,
    int socket_timeout_ms,
    const std::string& servers_ip) {
        auto socket_ptr = std::make_shared<CreateSocket>(servers_name_prefix, servers_port, servers_max_connections, socket_timeout_ms, servers_ip);
        if (!socket_ptr->Init_()) {
            return nullptr;
        }
        return socket_ptr;
}

int CreateSocket::GetFd() const {
    return imp_->fd_;
}

const std::string& CreateSocket::GetIp() const {
    return imp_->servers_ip_;
}

uint16_t CreateSocket::GetPort() const {
    return imp_->servers_port_;
}

bool CreateSocket::Init_() {
    imp_->fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (imp_->fd_ < 0) {
        LOG_ERROR("create socket error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    if (!SetSocketOpt()) {
        ::close(imp_->fd_);
        imp_->fd_ = -1;
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(imp_->servers_port_);

    if (imp_->servers_ip_.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, imp_->servers_ip_.c_str(), &addr.sin_addr) <= 0) {
            LOG_ERROR("invalid ip address: %s", imp_->servers_ip_.c_str());
            ::close(imp_->fd_);
            imp_->fd_ = -1;
            return false;
        }
    }

    // 绑定地址和端口
    if (::bind(imp_->fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind error, ip: {}, port: {}, errno: {}, error: {}",
                    imp_->servers_ip_.c_str(), imp_->servers_port_, errno, strerror(errno));
        ::close(imp_->fd_);
        imp_->fd_ = -1;
        return false;
    }

    // 开始监听
    if (::listen(imp_->fd_, imp_->servers_max_connections_) < 0) {
        LOG_ERROR("listen error, errno: %d, error: %s", errno, strerror(errno));
        ::close(imp_->fd_);
        imp_->fd_ = -1;
        return false;
    }
    return true;
}

} // namespace cookrpc
