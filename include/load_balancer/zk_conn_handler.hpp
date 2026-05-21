#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <zookeeper/zookeeper.h>
#include <zookeeper/zookeeper_version.h>
#include <zookeeper/proto.h>

#ifdef __cplusplus
}
#endif

#include <vector>
#include <string>
#include <memory>

#include <nlohmann/json.hpp>

#include "registry/services_registry.hpp"

namespace cookrpc {
    class ServiceRegistryConfig;

    class ZkConnHandler {
    public:
        static ZkConnHandler& GetInstance() {
            static ZkConnHandler instance;
            return instance;
        }
        ~ZkConnHandler();

        bool InitZkConnHandler(const nlohmann::json& config);
        std::vector<std::string> GetAllServices(const std::string& zk_namespace);
        std::string GetServices(const std::string& zk_namespace);
        bool RegisterService(const std::string& service_name, const std::string& service_address);
        bool RegisterServicesFromConfig(const ServiceRegistryConfig* config);

        ServiceRegistry* GetServiceRegistry();
        const ServiceRegistry* GetServiceRegistry() const;

        bool HasServiceRegistry() const;

        ServiceRegistry* GetOrCreateServiceRegistry();

        void SetZkHost(const std::string& host);
        void SetZkPort(int port);
        void SetZkRetryInterval(int seconds);
        void SetZkNamespace(const std::string& zk_namespace);

        void Cleanup();
        void UpdateServersFromZk(const std::string& zk_namespace);
        bool EnsureZkConnection();

        ZkConnHandler(const ZkConnHandler&) = delete;
        ZkConnHandler(const ZkConnHandler&&) = delete;
        ZkConnHandler& operator=(ZkConnHandler&) = delete;
        ZkConnHandler& operator=(ZkConnHandler&&) = delete;
    private:
        ZkConnHandler();
        static void GlobalWatcher_(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx);
        bool CreateServiceRegistryIfNeeded_();

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };
}
