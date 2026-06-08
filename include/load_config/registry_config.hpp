#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>

namespace cookrpc
{
    struct RegistryInfo
    {
        std::string address;
        uint16_t port;
    };

    class ServiceRegistryConfig
    {
    public:
        ~ServiceRegistryConfig();
        ServiceRegistryConfig();

        bool InitRegistryConfig(const std::string& config_file);

        void SetServiceName(const std::string& name);
        const std::string& GetServiceName() const;

        void SetServiceVersion(const std::string& version);
        const std::string& GetServiceVersion() const;

        const std::vector<RegistryInfo>& GetRegistryNodes() const;
        size_t GetRegistryNodesSize() const;

        ServiceRegistryConfig(const ServiceRegistryConfig&) = delete;
        ServiceRegistryConfig& operator=(const ServiceRegistryConfig&) = delete;
        ServiceRegistryConfig(ServiceRegistryConfig&&) = delete;
        ServiceRegistryConfig& operator=(ServiceRegistryConfig&&) = delete;

    private:
        struct Imp;
        std::unique_ptr<Imp> imp_;
    };
} // namespace cookrpc
