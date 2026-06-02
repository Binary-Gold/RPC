#pragma once

#include <memory>
#include <string>
#include <netinet/tcp.h>

#include "log_manager.hpp"
#include "load_config/rpc_server_config.hpp"
namespace cookrpc
{

    class CreateSocket
    {
    public:
        static std::shared_ptr<CreateSocket> Create(const std::string &servers_name_prefix, uint16_t servers_port, int servers_max_connections, int socket_timeout_ms, const std::string &servers_ip);
        // static std::shared_ptr<CreateSocket> Create(const cookrpc::RpcServerConfig &config);
        ~CreateSocket();
        int GetFd() const;
        const std::string &GetIp() const;
        uint16_t GetPort() const;
        CreateSocket(const std::string &servers_name_prefix,
                     uint16_t servers_port,
                     int servers_max_connections,
                     int socket_timeout_ms,
                     const std::string &servers_ip);
        // CreateSocket(const cookrpc::RpcServerConfig &config);

        int Accept();

    private:
        bool Init();
        bool SetSocketOpt();
        
        struct Imp;
        std::unique_ptr<Imp> imp_;
    };
}