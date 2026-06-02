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
    bool is_init_{false};
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
        if (!socket_ptr->Init()) {
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

int CreateSocket::Accept() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 接受新连接
    int client_fd = ::accept(imp_->fd_, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return -1; // 非阻塞模式下正常返回
        }
        LOG_ERROR("accept error, errno: {}, error: {}", errno, strerror(errno));
        return -1;
    }

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("set nonblock failed, fd: {}", client_fd);
        close(client_fd);
        return -1;
    }

    // 记录客户端信息
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    return client_fd;
}

bool CreateSocket::Init() {
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

    imp_->is_init_ = true;
    return true;
}

bool CreateSocket::SetSocketOpt() {
    // 设置发送超时
    struct timeval send_timeout;
    send_timeout.tv_sec = imp_->socket_timeout_ms_ / 1000;           // 转换为秒
    send_timeout.tv_usec = (imp_->socket_timeout_ms_ % 1000) * 1000; // 剩余毫秒转换为微秒
    if (setsockopt(imp_->fd_, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0) {
        LOG_ERROR("set send timeout error, errno: {}, error: {}", errno, strerror(errno));
        return false;
    }

    // 设置接收超时
    struct timeval recv_timeout;
    recv_timeout.tv_sec = imp_->socket_timeout_ms_ / 1000;
    recv_timeout.tv_usec = (imp_->socket_timeout_ms_ % 1000) * 1000;
    if (setsockopt(imp_->fd_, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        LOG_ERROR("set recv timeout error, errno: {}, error: {}", errno, strerror(errno));
        return false;
    }

    // 设置地址重用
    int reuse = 1;
    if (::setsockopt(imp_->fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_ERROR("set SO_REUSEADDR error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    // 设置 TCP NODELAY
    int nodelay = 1;
    if (::setsockopt(imp_->fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        LOG_ERROR("set TCP_NODELAY error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    // 设置非阻塞
    int flags = ::fcntl(imp_->fd_, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR("get socket flags error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    if (::fcntl(imp_->fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("set socket nonblock error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    // 设置 TCP keepalive
    int keepalive = 1;
    if (::setsockopt(imp_->fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        LOG_ERROR("set SO_KEEPALIVE error, errno: %d, error: %s", errno, strerror(errno));
        return false;
    }

    return true;
}

} // namespace cookrpc
