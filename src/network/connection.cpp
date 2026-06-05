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

    if (getpeername(imp_->fd_, (struct sockaddr *)&addr, &len) == 0) {
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
        HandleError_();
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
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

bool Connection::Write(const std::string& data) {
    // 检查连接状态
    if (imp_->state_ != State::CONNECTED) {
        LOG_ERROR("Connection is not in CONNECTED state for write, fd: {}, state: {}", 
                    imp_->fd_, static_cast<int>(imp_->state_));
        return false;
    }

    if (data.empty()) {
        LOG_ERROR("write data is empty, fd: {}", imp_->fd_);
        return false;
    }

    // 添加到写缓冲
    imp_->write_buffer_.insert(imp_->write_buffer_.end(), data.begin(), data.end());

    // 尝试发送数据
    while (!imp_->write_buffer_.empty()) {
        ssize_t n = write(imp_->fd_, imp_->write_buffer_.data(), imp_->write_buffer_.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            LOG_ERROR("write error, fd: {}, errno: {}", imp_->fd_, errno);
            imp_->state_ = State::DISCONNECTED;
            return false;
        }
        imp_->write_buffer_.erase(imp_->write_buffer_.begin(), imp_->write_buffer_.begin() + n);
    }
    return true;
}

bool Connection::Write(const RpcResponse& response) {
    // 1. Serialize
    std::string body;
    if (!response.Serialize(body)) {
        LOG_ERROR("serialize response failed");
        return false;
    }

    // 2. Compress
    std::string compressed;
    if (!ZstdCompress::getInstance().CompressString(body, compressed)) {
        LOG_ERROR("compress response failed");
        return false;
    }

    // 3. Encrypt
    std::string encrypted;
    if (!AesEncrypt::getInstance().Encrypt(compressed, encrypted)) {
        LOG_ERROR("encrypt response failed");
        return false;
    }

    // 4. Prepend header
    RpcHeader header;
    header.magic = RpcHeader::MAGIC;
    header.message_length = encrypted.size();
    header.sequence_id = response.getSequenceId();

    std::string frame;
    frame.resize(sizeof(header) + encrypted.size());
    std::memcpy(&frame[0], &header, sizeof(header));
    std::memcpy(&frame[sizeof(header)], encrypted.data(), encrypted.size());

    return Write(frame);
}

void Connection::Close() {
    if (imp_->fd_ >= 0) {
        if (imp_->state_ == State::CONNECTED) {
            imp_->state_ = State::DISCONNECTING;
        }
        
        ::close(imp_->fd_);
        imp_->fd_ = -1;
        imp_->state_ = State::DISCONNECTED;
    }
}

bool Connection::ReadWithTimeout(int timeout_ms) {
    while (true) {
        fd_set read_set;
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        // 每次循环都需要重新设置 fd_set
        FD_ZERO(&read_set);
        FD_SET(imp_->fd_, &read_set);

        int ret = select(imp_->fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (ret < 0) {
            if (errno == EINTR) {
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

int Connection::GetFd() const {
    return imp_->fd_;
}

const std::string& Connection::GetPeerAddress() const {
    return imp_->peer_addr_;
}

uint16_t Connection::GetPeerPort() const {
    return imp_->peer_port_;
}

Connection::State Connection::GetState() const {
    return imp_->state_;
}

std::string Connection::GetReadBufferData() {
    return std::string(imp_->read_buffer_.begin(), imp_->read_buffer_.end());
}

void Connection::SetMessageCallback(const MessageCallback& cb) {
    imp_->message_callback_ = cb;
}

void Connection::SetCloseCallback(const CloseCallback& cb) {
    imp_->close_callback_ = cb;
}

bool Connection::ProcessMessage() {
    // 循环处理
    while (imp_->read_buffer_.size() >= sizeof(RpcHeader)) {
        // 1. 读明文头 → 校验魔数
        RpcHeader header;
        std::memcpy(&header, imp_->read_buffer_.data(), sizeof(header));

        if (header.magic != RpcHeader::MAGIC) {
            LOG_ERROR("Magic number mismatch: {:#x}, fd: {}", header.magic, imp_->fd_);
            Close();
            return false;
        }

        size_t frame_size = sizeof(header) + header.message_length;
        if (imp_->read_buffer_.size() < frame_size) {
            break;  // 半包，等下次 Read 补全
        }

        // 2. 切出这一帧的加密体
        std::string encrypted_body(
            imp_->read_buffer_.begin() + sizeof(header),
            imp_->read_buffer_.begin() + frame_size);

        // 3. 解密 → 解压 → 反序列化
        std::string decrypted;
        if (!AesEncrypt::getInstance().Decrypt(encrypted_body, decrypted)) {
            LOG_ERROR("Failed to decrypt, fd: {}", imp_->fd_);
            imp_->read_buffer_.erase(imp_->read_buffer_.begin(),
                                     imp_->read_buffer_.begin() + frame_size);
            return false;
        }

        std::string decompressed;
        if (!ZstdCompress::getInstance().DecompressString(decrypted, decompressed)) {
            LOG_ERROR("Failed to decompress, fd: {}", imp_->fd_);
            imp_->read_buffer_.erase(imp_->read_buffer_.begin(),
                                     imp_->read_buffer_.begin() + frame_size);
            return false;
        }

        RpcRequest request;
        request.setSequenceId(header.sequence_id);
        if (!request.Deserialize(decompressed)) {
            LOG_ERROR("Failed to deserialize, fd: {}", imp_->fd_);
            imp_->read_buffer_.erase(imp_->read_buffer_.begin(),
                                     imp_->read_buffer_.begin() + frame_size);
            return false;
        }

        // 4. 处理完 → 从这个帧切掉
        imp_->read_buffer_.erase(imp_->read_buffer_.begin(),
                                 imp_->read_buffer_.begin() + frame_size);

        HandleMessage_(request);
    }
    return true;
}

void Connection::HandleMessage_(const RpcRequest& request) {
    if (!imp_->message_callback_) {
        LOG_WARN("No message callback set, fd: {}", imp_->fd_);
        return;
    }

    try {
        imp_->message_callback_(shared_from_this(), request);
    } catch (const std::exception &e) {
        LOG_ERROR("Message callback failed: {}, service: {}, method: {}, fd: {}",
                    e.what(), request.getServiceName(), request.getMethodName(), imp_->fd_);
    } catch (...) {
        LOG_ERROR("Unknown exception in message callback, fd: {}", imp_->fd_);
    }
}

void Connection::HandleError_() {
    if (imp_->close_callback_) {
        imp_->close_callback_(shared_from_this());
    }
    Close();
}

} // namespace cookrpc
