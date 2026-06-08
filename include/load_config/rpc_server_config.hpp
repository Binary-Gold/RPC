#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace cookrpc
{
    class ServiceRegistryConfig;
    class ThreadPoolConfig;
    class ZkConnHandler;
    class LoadBalancer;

    class RpcServerConfig
    {
    public:
        static RpcServerConfig& GetInstance();
        ~RpcServerConfig();

        bool InitRpcServerConfig(const std::string& config_file);

        const std::string& GetServersNamePrefix() const;
        const std::string& GetServersIp() const;
        uint16_t GetServersPort() const;
        int GetTimeout() const;
        int GetMaxConnections() const;

        const ServiceRegistryConfig* GetServiceRegistryConfig() const;
        const ThreadPoolConfig* GetThreadPoolConfig() const;

        void SetServersNamePrefix(const std::string& servers_name_prefix);
        void SetServersIp(const std::string& servers_ip);
        void SetServersPort(uint16_t servers_port);
        void SetTimeout(uint32_t timeout);
        void SetMaxConnections(uint16_t max_connections);

        RpcServerConfig(const RpcServerConfig&) = delete;
        RpcServerConfig& operator=(const RpcServerConfig&) = delete;
        RpcServerConfig(RpcServerConfig&&) = delete;
        RpcServerConfig& operator=(RpcServerConfig&&) = delete;

    private:
        RpcServerConfig();

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

} // namespace cookrpc
