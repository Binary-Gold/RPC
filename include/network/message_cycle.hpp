#pragma once

#include <pthread.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>

#include <set>
#include <mutex>
#include <atomic>
#include <memory>
#include <future>

#include "network/connection_manager.hpp"
#include "network/connection.hpp"
#include "protocol/rpc_protocol.hpp"
#include "core/error_code.hpp"
#include "log_manager.hpp"
#include "service/service_manager.hpp"
#include "thread_pool/thread_pool_singleton.hpp"

namespace cookrpc
{

    class MessageCycle
    {
    public:
        MessageCycle(ConnectionManager* manager);
        virtual ~MessageCycle();
        bool AddListenFd(int fd);
        bool RemoveListenFd(int fd);
        size_t GetListenFdCount() const;
        void Loop();
        void Stop();
        void HandleRpcRequest(const std::shared_ptr<Connection>& conn,
                              const RpcRequest& request);
        void HandleRpcRequestAsync(const std::shared_ptr<Connection>& conn,
                                   const RpcRequest& request);

    private:
        void HandleEpollEvents_(struct epoll_event* events, int nfds);
        void HandleNewConnection_(int fd);
        void HandleClientData_(int fd);
        void RemoveConnection_(int fd);
        void RemoveInvalidFileDescriptor_(int fd);
        bool IsValidFileDescriptor_(int fd);
        bool ValidateRequest_(const RpcRequest& request);
        void SendSuccessResponse_(const std::shared_ptr<Connection>& conn,
                                 uint64_t sequence_id,
                                 const std::string& result);
        void SendErrorResponse_(const std::shared_ptr<Connection>& conn,
                               uint64_t sequence_id,
                               ErrorCode error_code,
                               const std::string& error_message);
        void SendResponse_(const std::shared_ptr<Connection>& conn,
                         const RpcResponse& response,
                         const std::string& response_type);
        void HandleRpcRequestSync_(const std::shared_ptr<Connection>& conn,
                                  const RpcRequest& request);

        struct Imp;
        std::unique_ptr<Imp> imp_;

        static const int MAX_EVENTS = 1024;
    };

} // namespace cookrpc
