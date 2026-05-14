#include "registry/services_registry.hpp"
#include "log_manager.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include <zookeeper/zookeeper.h>
#include <zookeeper/zookeeper.jute.h>

struct ServiceRegistry::Imp
{
    static constexpr std::string ROOT_PATH = "/cookrpc";
    bool is_connect_{false};
    zhandle_t* zk_handle_{nullptr};
};

ServiceRegistry::ServiceRegistry(const std::string& zk_hosts) : imp_(std::make_unique<Imp>()) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);

    imp_->zk_handle_ = zookeeper_init(zk_hosts.c_str(), 
                                    GlobalWatcher, 
                                    3000, 
                                    0,
                                    this,
                                    0);
    if (!imp_->zk_handle_) {
        throw std::runtime_error("Failed to connect to ZooKeeper");
    }

    for (int i = 0; i < 10; ++i) {
        if (imp_->is_connect_) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!imp_->is_connect_) {
        LOG_WARN("Zookeeper connection timeout, but continuting...");
    } else {
        LOG_INFO("Connected to Zookeeper: {}", zk_hosts);
    }
}

ServiceRegistry::~ServiceRegistry() {
    if (imp_->zk_handle_) {
        try {
            LOG_INFO("Closing ZooKeeper connection");
            zookeeper_close(imp_->zk_handle_);
            imp_->zk_handle_ = nullptr;
            LOG_INFO("ZooKeeper connection closed successfully");
        } catch (const std::exception& e) {
            std::cerr << "Exception closing ZooKeeper: " << e.what() << std::endl;
            imp_->zk_handle_ = nullptr;
        } catch(...) {
            std::cerr << "Unknown closing ZooKeeper" << std::endl;
            imp_->zk_handle_ = nullptr;
        }
    }
}

void ServiceRegistry::GlobalWatcher_(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (!watcherCtx) {
        return;
    }    
    ServiceRegistry *registry = static_cast<ServiceRegistry*>(watcherCtx);
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            registry->imp_->is_connect_ = true;
            LOG_INFO("Connected to ZooKeeper");
        } else if(type == ZOO_EXPIRED_SESSION_STATE) {
            registry->imp_->is_connect_ = false;
            LOG_ERROR("ZooKeeper session expried");
        } else if(type == ZOO_AUTH_FAILED_STATE) {
            registry->imp_->is_connect_ = false;
            LOG_ERROR("ZooKeeper authentication failed");
        } else {
            registry->imp_->is_connect_ = false;
            LOG_WARN("ZooKeeper connection state changed: {}", state);
        }
    } 
}

bool ServiceRegistry::EnsurePath_(const std::string& path) {
    if (path.empty()) {
        return true;
    }

    if (!imp_->zk_handle_) {
        LOG_ERROR("ZooKeeper handle is nullptr");
        return false;
    }

    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0) {
        std::string parent = path.substr(0, pos);
        if (!parent.empty() && !EnsurePath(parent)) {
            LOG_ERROR("Failed to create parent path: {}", parent);
            return false;
        }
    }

    struct Stat stat;
    int ret = zoo_exists(imp_->zk_handle_, path.c_str(), 0, &stat);

    if (ret == ZOK) {
        LOG_DEBUG("Path exists: {}", path);
        return true;
    } else if(ret == ZNONODE) {
        ret = zoo_create(imp_->zk_handle_,
                        path.c_str(),
                        "",
                        0,
                        &ZOO_OPEN_ACL_UNSAFE,
                        0,
                        NULL,
                        0);
        if (ret == ZOK) {
            LOG_INFO("Created persistent path: {}", path);
            return true;
        } else if (ret == ZNODEEXISTS) {
            LOG_DEBUG("Path already exists (race condition): {}", path);
            return true;
        } else {
            LOG_ERROR("Failed to create path: {}, error: {}", path, zerror(ret));
            return false;
        }
    }
    else {
        LOG_ERROR("Failed to check path: {}, error: {}", path, zerror(ret));
        return false;
    }
}

bool ServiceRegistry::CreateNode_(const std::string& path, const std::string& data, int flags) {
    if (!imp_->zk_handle_) {
        return false;
    }
    int ret = zoo_create(imp_->zk_handle_,
                        path.c_str(),
                        data.c_str(),
                        data.length(),
                        &ZOO_OPEN_ACL_UNSAFE,
                        flags,
                        NULL,
                        0);

    if (ret == ZOK) {
        LOG_INFO("Created persistent path: {} (flags={})", path, flags);
        return true;
    } else if (ret == ZNODEEXISTS) {
        LOG_DEBUG("Path already exists (race condition): {}", path);
        return true;
    } else {
        LOG_ERROR("Failed to create path: {}, error: {}", path, zerror(ret));
        return false;
    }
}

bool ServiceRegistry::RegisterService(const std::string& service_name, const std::string& service_address) {
    if (!IsConnected() || !imp_->zk_handle_) {
        LOG_ERROR("Not connected to ZooKeeper");
        return false;
    }

    std::string service_path = imp_->ROOT_PATH + "/" + service_name;
    if (!EnsurePath_(service_path)) {
        LOG_ERROR("Failed to ensure service path: {}", service_path);
        return false;
    }

    std::string instance_path = service_path + "/" + service_address;
    if (!CreateNode_(instance_path, service_address, ZOO_EPHEMERAL)) {
        LOG_ERROR("Failed to create service instance: {}", instance_path);
        return false;
    }

    LOG_INFO("Service registered: {} at {}", service_name, service_address);
    return true;
}

bool ServiceRegistry::IsConnected() const {
    return imp_->is_connect_ && imp_->zk_handle_;
}



