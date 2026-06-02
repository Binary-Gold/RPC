#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

#include "protocol/rpc_protocol.hpp"
#include "log_manager.hpp"

namespace cookrpc
{

    class RpcMessage;
    class RpcRequest;
    class RpcResponse;

    class Connection : public std::enable_shared_from_this<Connection>
    {
    public:
        enum class State {
            CONNECTED,
            DISCONNECTING,
            DISCONNECTED
        };

        // 定义消息回调函数类型
        using MessageCallback = std::function<void(const std::shared_ptr<Connection> &, const RpcRequest &)>;
        using CloseCallback = std::function<void(const std::shared_ptr<Connection> &)>;

        explicit Connection(int fd);
        ~Connection();

        // 基础 IO 操作
        bool Read();
        bool Write(const std::string &data);
        bool Write(const RpcResponse &response);
        void Close();
        bool IsValid() const;
        bool ReadWithTimeout(int timeout_ms);

        // 获取连接信息
        int GetFd() const;
        const std::string &GetPeerAddress() const;
        uint16_t GetPeerPort() const;
        State GetState() const;

        // 获取读缓冲区数据（客户端使用）
        std::string GetReadBufferData();

        // 设置回调
        void SetMessageCallback(const MessageCallback &cb);
        void SetCloseCallback(const CloseCallback &cb);

        static const size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB
        bool ProcessMessage();

    private:
        static const size_t MAX_BUFFER_SIZE = 65536;

        void HandleMessage(const RpcRequest &request);
        void HandleError();
        bool SendInBuffer();

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

} // namespace cookrpc