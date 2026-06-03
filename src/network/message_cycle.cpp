
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "network/message_cycle.hpp"
#include "encrypt/aes_encrypt.hpp"
#include "compress_data/zstd_compress.hpp"
#include "thread_pool/thread_pool.hpp"

namespace cookrpc
{

struct MessageCycle::Imp {
    pthread_t thread_id_;
    std::set<int> listen_fds_;
    std::atomic<bool> running_{true};
    mutable std::mutex mutex_;
    int epoll_fd_{-1};
    ConnectionManager* connection_manager_{nullptr};
};

// 定义一个原子标志来指示是否应该停止
static std::atomic<bool> stop_flag(false);

    // 信号处理函数需要在命名空间外部
    static void signalHandler(int signum)
    {
        stop_flag = true;
    }
    
    MessageCycle::MessageCycle(ConnectionManager* manager)
        : imp_(std::make_unique<Imp>())
    {
        imp_->connection_manager_ = manager;

        imp_->epoll_fd_ = epoll_create1(0);
        if (imp_->epoll_fd_ == -1) {
            LOG_ERROR("Failed to create epoll: {} (errno: {})", strerror(errno), errno);
            throw std::runtime_error("Failed to create epoll");
        }

        // 注册信号处理函数
        if (::signal(SIGINT, signalHandler) == SIG_ERR)
        {
            LOG_ERROR("Failed to register signal handler");
            throw std::runtime_error("Signal handler registration failed");
        }
    }

    void MessageCycle::HandleEpollEvents(struct epoll_event *events, int nfds)
    {
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            // // LOG_INFO("handle epoll events, fd: {}", fd);

            if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                LOG_ERROR("event error on fd: {}", fd);
                RemoveConnection(fd);
                continue;
            }

            // 使用listen_fds_集合判断是否为监听socket
            {
                std::lock_guard<std::mutex> lock(imp_->mutex_);
                if (imp_->listen_fds_.find(fd) != imp_->listen_fds_.end())
                {
                    // // LOG_INFO("handle epoll event new connection, fd: {}", fd);
                    HandleNewConnection(fd);
                    continue;
                }
            }
            // // LOG_INFO("handle epoll event success,before handle client data, fd: {}", fd);

            // 处理客户端数据
            if (events[i].events & EPOLLIN)
            {
                HandleClientData(fd);
            }
        }
    }


    bool MessageCycle::AddListenFd(int fd)
    {
        if (fd < 0)
        {
            LOG_ERROR("add listen fd is error");
            return false;
        }

        std::lock_guard<std::mutex> lock(imp_->mutex_);

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(imp_->epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            LOG_ERROR("Failed to add fd {} to epoll: {}", fd, strerror(errno));
            return false;
        }

        imp_->listen_fds_.insert(fd);
        return true;
    }

    bool MessageCycle::RemoveListenFd(int fd)
    {
        std::lock_guard<std::mutex> lock(imp_->mutex_);

        if (imp_->listen_fds_.find(fd) == imp_->listen_fds_.end())
        {
            LOG_ERROR("File descriptor {} not found remove listen fd", fd);
            return false;
        }

        struct epoll_event ev;
        if (epoll_ctl(imp_->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1)
        {
            LOG_ERROR("Failed to remove fd {} from epoll: {}", fd, strerror(errno));
        }

        imp_->listen_fds_.erase(fd);
        return true;
    }

    size_t MessageCycle::GetListenFdCount() const
    {
        std::lock_guard<std::mutex> lock(imp_->mutex_);
        return imp_->listen_fds_.size();
    }

    MessageCycle::~MessageCycle()
    {
        // // LOG_INFO("MessageCycle destructor called");
        
        // 停止运行
        imp_->running_ = false;
        stop_flag = true;

        try {
            std::lock_guard<std::mutex> lock(imp_->mutex_);
            
            // 清理监听文件描述符
            for (int fd : imp_->listen_fds_)
            {
                try {
                    epoll_ctl(imp_->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

                    close(fd);
                    LOG_DEBUG("Closed listen fd: {}", fd);
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception closing listen fd {}: {}", fd, e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception closing listen fd: {}", fd);
                }
            }
            imp_->listen_fds_.clear();

            // 清理事件循环文件描述符
            if (imp_->epoll_fd_ >= 0)
            {
                close(imp_->epoll_fd_);
                imp_->epoll_fd_ = -1;
                LOG_DEBUG("Closed epoll fd");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in MessageCycle destructor: {}", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in MessageCycle destructor");
        }
        
        // // LOG_INFO("MessageCycle destructor completed");
    }

    void MessageCycle::HandleNewConnection(int listen_fd)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 接受新连接
        int client_fd = ::accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0)
        {
            LOG_ERROR("Accept failed, errno: {}, error: {}", errno, strerror(errno));
            return;
        }

        // LOG_INFO("New connection from {}:{}, fd: {}",
                //  inet_ntoa(client_addr.sin_addr),
                //  ntohs(client_addr.sin_port),
                //  client_fd);

        try
        {
            // 设置非阻塞
            int flags = fcntl(client_fd, F_GETFL, 0);
            if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            {
                throw std::runtime_error("Set nonblock failed");
            }

            // 设置TCP选项
            int optval = 1;
            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0)
            {
                throw std::runtime_error("Set TCP_NODELAY failed");
            }

            // 创建连接对象
            auto conn = std::make_shared<Connection>(client_fd);

            // 设置回调
            conn->SetMessageCallback(
                [this](const std::shared_ptr<Connection> &conn, const RpcRequest &request)
                {
                    HandleRpcRequest(conn, request);
                });

            conn->SetCloseCallback(
                [this](const std::shared_ptr<Connection> &conn)
                {
                    RemoveConnection(conn->GetFd());
                });

            // 添加到连接管理器
            imp_->connection_manager_->AddConnection(conn);

            // 注册到事件循环
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET; // 使用边缘触发
            ev.data.fd = client_fd;
            if (epoll_ctl(imp_->epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) == -1)
            {
                throw std::runtime_error("Failed to add to epoll");
            }

            // LOG_INFO("Successfully added new connection, fd: {}", client_fd);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to handle new connection: {}, fd: {}", e.what(), client_fd);
            RemoveConnection(client_fd);
        }
    }

    void MessageCycle::HandleRpcRequest(const std::shared_ptr<Connection> &conn,
                                        const RpcRequest &request)
    {
        // 默认使用异步处理来提高性能
        HandleRpcRequestAsync(conn, request);
    }

    void MessageCycle::HandleRpcRequestAsync(const std::shared_ptr<Connection> &conn,
                                           const RpcRequest &request)
    {
        // 确保线程池已初始化
        if (!meeting_ctrl::ThreadPoolSingleton::Exists()) {
            meeting_ctrl::ThreadPoolSingleton::Init(std::thread::hardware_concurrency());
            LOG_INFO("ThreadPool initialized in MessageCycle with {} threads", 
                    std::thread::hardware_concurrency());
        }

        // 使用高优先级异步处理RPC请求
        auto future = meeting_ctrl::ThreadPoolSingleton::GetInstance().Enqueue(
            meeting_ctrl::TaskPriority::HIGH,
            [this, conn, request]() {
                this->HandleRpcRequestSync(conn, request);
            }
        );

        // 检查任务是否成功提交
        if (!future.valid()) {
            LOG_ERROR("Failed to enqueue RPC request - ThreadPool may be stopped: service={}, method={}, sequence={}",
                      request.getServiceName(), request.getMethodName(), request.getSequenceId());
            
            // 线程池不可用时，直接发送错误响应
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              "Service temporarily unavailable");
            return;
        }

        LOG_DEBUG("RPC request enqueued for async processing: service={}, method={}, sequence={}",
                  request.getServiceName(), request.getMethodName(), request.getSequenceId());
    }

    void MessageCycle::HandleRpcRequestSync(const std::shared_ptr<Connection> &conn,
                                          const RpcRequest &request)
    {
        RpcResponse response;
        response.setSequenceId(request.getSequenceId()); // 设置相同的序列号

        try
        {
            // 参数验证
            if (!ValidateRequest(request))
            {
                SendErrorResponse(conn, request.getSequenceId(),
                                  ErrorCode::INVALID_REQUEST,
                                  Error::getErrorMessage(ErrorCode::INVALID_REQUEST));
                return;
            }

            // 使用ServiceManager的同步处理方法，避免双重异步
            std::string result;
            bool success = ServiceManager::GetInstance().HandleRpcRequest(
                request.getServiceName(),
                request.getMethodName(), 
                request.getPayload(),
                result
            );

            if (!success) {
                LOG_ERROR("Failed to handle request: method={}", request.getMethodName());
                SendErrorResponse(conn, request.getSequenceId(),
                                  ErrorCode::SERVICE_NOT_FOUND,
                                  Error::getErrorMessage(ErrorCode::SERVICE_NOT_FOUND));
                return;
            }

            // 发送成功响应
            SendSuccessResponse(conn, request.getSequenceId(), result);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception while handling RPC request: {}", e.what());
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              Error::getErrorMessage(ErrorCode::INTERNAL_ERROR));
        }
        catch (...)
        {
            LOG_ERROR("Unknown exception while handling RPC request");
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              Error::getErrorMessage(ErrorCode::INTERNAL_ERROR));
        }
    }

    bool MessageCycle::ValidateRequest(const RpcRequest &request)
    {
        return !request.getServiceName().empty() &&
               !request.getMethodName().empty() &&
               request.getSequenceId() > 0;
    }

    void MessageCycle::SendErrorResponse(const std::shared_ptr<Connection> &conn,
                                         uint64_t sequence_id,
                                         ErrorCode error_code,
                                         const std::string &error_message)
    {
        RpcResponse response;
        response.setSequenceId(sequence_id);
        response.setErrorCode(static_cast<uint32_t>(error_code));
        response.setErrorMessage(error_message);
        
        SendResponse(conn, response, "error");
    }

    void MessageCycle::SendSuccessResponse(const std::shared_ptr<Connection> &conn,
                                           uint64_t sequence_id,
                                           const std::string &result)
    {
        RpcResponse response;
        response.setSequenceId(sequence_id);
        response.setErrorCode(static_cast<uint32_t>(cookrpc::ErrorCode::SUCCESS));
        response.setErrorMessage(cookrpc::Error::getErrorMessage(cookrpc::ErrorCode::SUCCESS));
        response.setResultData(result);

        SendResponse(conn, response, "success");
    }

    void MessageCycle::SendResponse(const std::shared_ptr<Connection> &conn,
                                   const RpcResponse &response,
                                   const std::string &response_type)
    {
        // 序列化响应
        std::string serialized_data;
        if (!response.Serialize(serialized_data))
        {
            LOG_ERROR("Failed to serialize {} response", response_type);
            return;
        }
        
        // 压缩数据
        std::string compressed_data;
        if (!ZstdCompress::getInstance().CompressString(serialized_data, compressed_data))
        {
            LOG_ERROR("Failed to compress {} response", response_type);
            return;
        }
        
        // 加密数据
        std::string encrypted_data;
        if (!AesEncrypt::getInstance().Encrypt(compressed_data, encrypted_data))
        {
            LOG_ERROR("Failed to encrypt {} response", response_type);
            return;
        }
        
        if (!conn->Write(encrypted_data))
        {
            LOG_ERROR("Failed to send {} response: sequence_id={}", response_type, response.getSequenceId());
        }
    }

    void MessageCycle::HandleClientData(int fd)
    {
        auto conn = imp_->connection_manager_->GetConnection(fd);
        if (!conn)
        {
            LOG_ERROR("Connection not found for fd handle client data: {}", fd);
            RemoveConnection(fd);
            return;
        }

        // 检查连接是否有效
        if (!conn->IsValid())
        {
            LOG_ERROR("Connection is invalid, fd: {}, state: {}", fd, static_cast<int>(conn->GetState()));
            RemoveConnection(fd);
            return;
        }

        // 读取数据
        if (!conn->Read())
        {
            // LOG_ERROR("Failed to read data from fd: {}", fd);
            RemoveConnection(fd);
            return;
        }

        // 处理读取到的数据
        if (!conn->ProcessMessage())
        {
            LOG_ERROR("Failed to process message from fd: {}", fd);
            RemoveConnection(fd);
            return;
        }
    }

    void MessageCycle::RemoveConnection(int fd)
    {
        // 首先从epoll中移除文件描述符
        RemoveInvalidFileDescriptor(fd);
        
        // 然后从连接管理器中移除连接
        imp_->connection_manager_->RemoveConnection(fd);
    }

    void MessageCycle::Loop()
    {
        LOG_INFO("Message cycle loop started");
        imp_->running_ = true; // 确保循环开始时 imp_->running_ 为 true

        const int MAX_EVENTS = 1024;
        while (imp_->running_ && !stop_flag.load())  // 检查两个停止条件
        {
            struct epoll_event events[MAX_EVENTS];
            int nev = epoll_wait(imp_->epoll_fd_, events, MAX_EVENTS, 1000); // 1秒超时

            if (nev < 0)
            {
                if (errno == EINTR || stop_flag.load())  // 信号中断时也退出
                {
                    // LOG_INFO("Message cycle interrupted by signal");
                    break;
                }
                LOG_ERROR("epoll_wait error: {} (errno: {})", strerror(errno), errno);
                break;
            }
            // // LOG_INFO("epoll_wait nev");

            if (nev > 0 && !stop_flag.load())  // 只有在没有停止信号时才处理事件
            {
                HandleEpollEvents(events, nev);
            }
            
            // 定期检查停止标志
            if (stop_flag.load())
            {
                // LOG_INFO("Stop flag detected, exiting message loop");
                break;
            }
        }

        imp_->running_ = false;
        // LOG_INFO("Message cycle loop ended");
    }

    bool MessageCycle::IsValidFileDescriptor(int fd)
    {
        if (fd < 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(imp_->mutex_);

        // 检查是否是监听 socket
        if (imp_->listen_fds_.find(fd) != imp_->listen_fds_.end())
        {
            return true;
        }

        // 检查是否是有效的客户端连接
        if (imp_->connection_manager_->GetConnection(fd))
        {
            return true;
        }

        LOG_ERROR("fd {} is neither in listen_fds_ nor in connections", fd);
        return false;
    }

    void MessageCycle::RemoveInvalidFileDescriptor(int fd)
    {
        std::lock_guard<std::mutex> lock(imp_->mutex_);
        
        epoll_ctl(imp_->epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }

    void MessageCycle::Stop()
    {
        // LOG_INFO("Stop method called, setting flags to stop message cycle");
        imp_->running_ = false;
        stop_flag = true;
    }

}