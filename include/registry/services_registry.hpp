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
};