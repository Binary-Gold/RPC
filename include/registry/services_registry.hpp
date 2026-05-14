#pragma once

#include <string>
#include <vector>
#include <memory>
#include <zookeeper/zookeeper.h>
#include "log_manager.hpp"

class ServiceRegistry {
public:
    ServiceRegistry(const std::string& zk_hosts);
    ~ServiceRegistry();

    bool RegisterService(const std::string& service_name, const std::string& service_address);
    bool IsConnected() const;
private:
    static void GlobalWatcher_(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx);
    bool CreateNode_(const std::string& path, const std::string& data, int flags = 0);
    bool EnsurePath_(const std::string& path);

    struct Imp;
    std::unique_ptr<Imp> imp_;
};