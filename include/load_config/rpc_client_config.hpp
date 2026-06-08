#pragma once

#include <string>
#include <cstdint>
#include <memory>

namespace cookrpc
{

    class RpcClientConfig
    {
    public:
        static RpcClientConfig& GetInstance();
        ~RpcClientConfig();
        bool InitRpcClientConfig(const std::string& config_filename);

        RpcClientConfig(const RpcClientConfig&) = delete;
        RpcClientConfig& operator=(const RpcClientConfig&) = delete;
        RpcClientConfig(RpcClientConfig&&) = delete;
        RpcClientConfig& operator=(RpcClientConfig&&) = delete;

        void SetTimeout(int timeout_ms);
        void SetRetryTimes(int retry_times);
        void SetZkNamespace(const std::string& zk_namespace);
        void SetServerPort(int server_port);
        void SetZkHost(const std::string& zk_host);
        void SetZkPort(int zk_port);

        int GetTimeout() const;
        int GetRetryTimes() const;
        const std::string& GetZkNamespace() const;
        int GetServerPort() const;
        const std::string& GetZkHost() const;
        int GetZkPort() const;

    private:
        RpcClientConfig() = default;

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

}
