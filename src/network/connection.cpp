#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "serializer/protobuf_serializer.h"
#include "encrypt/aes_encrypt.hpp"
#include "compress_data/zstd_compress.hpp"
#include "network/connection.hpp"

namespace cookrpc {

struct Connection::Imp {
    int fd_ = -1;
    std::string peer_addr_ = "";
    uint16_t peer_port_ = 0;

    std::vector<char> read_buffer_;
    std::vector<char> write_buffer_;

    MessageCallback message_callback_ = nullptr;
    CloseCallback close_callback_ = nullptr;

    State state_ = State::DISCONNECTED;
    std::atomic<bool> is_writing_ = false;
};

Connection::Connection(int fd)
    : imp_(std::make_unique<Imp>()) {
    // 先检查 fd 的有效性
    if (imp_->fd_ < 0) {
        LOG_ERROR("connection fd is invalid: {}", imp_->fd_);
        imp_->state_ = State::DISCONNECTED;
        return;
    }

    // 检查是否是 socket
    int type;
    socklen_t type_len = sizeof(type);
    if (getsockopt(imp_->fd_, SOL_SOCKET, SO_TYPE, &type, &type_len) < 0) {
        LOG_ERROR("Not a socket fd: {}, errno: {}", imp_->fd_, errno);
        imp_->state_ = State::DISCONNECTED;
        return;
    }

    // 获取对端地址信息
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getpeername(imp_->fd_, (struct sockaddr *)&addr, &len) == 0)
    {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        imp_->peer_addr_ = ip;
        imp_->peer_port_ = ntohs(addr.sin_port);
        imp_->state_ = State::CONNECTED;
        // // LOG_INFO("Connection established with {}:{}, fd: {}", peer_addr_, peer_port_, fd_);
    } else {
        LOG_ERROR("getpeername failed, fd: {}, errno: {} ({})",
                    imp_->fd_, errno, strerror(errno));
        imp_->state_ = State::DISCONNECTED;
    }   
}

Connection::~Connection() {
    try {
        Close();
    } catch (const std::exception& e) {
        std::cerr << "Exception in Connection destructor: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception in Connection destructor" << std::endl;
    }
}

bool Connection::Read() {
    // 检查连接状态
    if (imp_->state_ != State::CONNECTED) {
        LOG_ERROR("Connection is not in CONNECTED state, fd: {}, state: {}", 
                    imp_->fd_, static_cast<int>(imp_->state_));
        return false;
    }

    char buf[4096];
    bool has_read_data = false;
    
    // 在边缘触发模式下，需要循环读取直到EAGAIN
    while (true) {
        ssize_t n = read(imp_->fd_, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 在边缘触发模式下，这表示所有数据都已读取完毕
                LOG_DEBUG("No more data available now, fd: {} (read {} bytes total)", 
                            fd_, has_read_data ? "some" : "no");
                return true;
            }
            LOG_ERROR("read error, fd: {}, errno: {}, error: {}", imp_->fd_, errno, strerror(errno));
            imp_->state_ = State::DISCONNECTED;
            return false;
        }

        if (n == 0) {
            imp_->state_ = State::DISCONNECTED;
            return false; // 连接关闭
        }

        has_read_data = true;
        LOG_DEBUG("Read {} bytes from fd: {}", n, fd_);

        // 检查缓冲区大小
        if (imp_->read_buffer_.size() + n > MAX_BUFFER_SIZE) {
            LOG_ERROR("read buffer overflow, fd: {}, current size: {}, trying to add: {}", 
                        imp_->fd_, imp_->read_buffer_.size(), n);
            imp_->state_ = State::DISCONNECTED;
            return false;
        }

        imp_->read_buffer_.insert(imp_->read_buffer_.end(), buf, buf + n);
    }
}

bool Connection::Write(const std::string& data)
{}

bool Connection::Write(const RpcResponse& response)
{}

void Connection::Close()
{}

bool Connection::IsValid() const
{}

bool Connection::ReadWithTimeout(int timeout_ms) {
    while (true){
        fd_set read_set;
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        // 每次循环都需要重新设置 fd_set
        FD_ZERO(&read_set);
        FD_SET(imp_->fd_, &read_set);

        int ret = select(imp_->fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (ret < 0) {
            if (errno == EINTR)
            {
                // 如果是被信号中断，则继续
                continue;
            }
            LOG_ERROR("select error, fd: {}, errno: {}", imp_->fd_, errno);
            return false;
        }

        if (ret == 0) {
            // 超时
            LOG_ERROR("read timeout after {} ms, fd: {}", timeout_ms, imp_->fd_);
            return false;
        }

        // 有数据可读，调用已有的 Read 函数
        if (!Read()) {
            LOG_ERROR("read failed after select, fd: {}", imp_->fd_);
            return false;
        }

        return true;
    }
}

int Connection::GetFd() const
{}

const std::string& Connection::GetPeerAddress() const
{}

uint16_t Connection::GetPeerPort() const
{}

Connection::State Connection::GetState() const
{}

std::string Connection::GetReadBufferData()
{}

void Connection::SetMessageCallback(const MessageCallback& cb)
{}

void Connection::SetCloseCallback(const CloseCallback& cb)
{}

bool Connection::ProcessMessage() {
    if (imp_->read_buffer_.size() >= sizeof(RpcHeader)) {
        try
        {
            // vector<char> 转换为 string
            std::string encrypted_data(imp_->read_buffer_.begin(), imp_->read_buffer_.end());

            // 解密数据
            std::string decrypted_data;

            if (encrypted_data.empty()) {
                LOG_ERROR("Encrypted data is empty, fd: {}", imp_->fd_);
                return false;
            }

            if (!AesEncrypt::getInstance().Decrypt(encrypted_data, decrypted_data)) {
                LOG_ERROR("Failed to decrypt data, encrypted size: {}, fd: {}",
                            encrypted_data.size(), imp_->fd_);
                return false;
            }

            // 检查解密后的数据
            if (decrypted_data.empty())
            {
                LOG_ERROR("Decrypted data is empty, fd: {}", imp_->fd_);
                return false;
            }

            std::string decompressed_data;
            if (!ZstdCompress::getInstance().DecompressString(decrypted_data, decompressed_data))
            {
                LOG_ERROR("Failed to decompress data, decrypted size: {}, fd: {}", 
                            decrypted_data.size(), imp_->fd_);
                return false;
            }

            // 检查数据长度是否足够包含头部
            if (decompressed_data.size() < sizeof(RpcHeader))
            {
                LOG_DEBUG("Waiting for more data, current size: {}, need: {}",
                            decompressed_data.size(), sizeof(RpcHeader));
                return true; // 数据不完整，等待更多数据
            }

            RpcRequest request;
            if (!request.Deserialize(decompressed_data))
            {
                LOG_ERROR("Failed to deserialize request body, decompressed size: {}, fd: {}", 
                            decompressed_data.size(), imp_->fd_);
                return false;
            }

            // // LOG_INFO("ProcessMessage: successfully deserialized request, fd: {}", fd_);

            // 处理消息
            HandleMessage(request);

            // 清理已处理的数据
            read_buffer_.clear(); // 因为我们已经处理完整个加密数据
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Error in ProcessMessage: {}, fd: {}", e.what(), fd_);
            return false;
        }
    }
    return true;
}

void Connection::HandleMessage(const RpcRequest& request)
{}

void Connection::HandleError()
{}

bool Connection::SendInBuffer()
{}

} // namespace cookrpc
