#include <fstream>
#include <nlohmann/json.hpp>

#include "log_manager.hpp"
#include "load_config/registry_config.hpp"

namespace cookrpc
{

struct ServiceRegistryConfig::Imp {
    std::vector<RegistryInfo> registry_nodes_;
    std::string service_name_;
    std::string service_version_;
};

ServiceRegistryConfig::ServiceRegistryConfig() : imp_(std::make_unique<Imp>()) {}
ServiceRegistryConfig::~ServiceRegistryConfig() = default;

bool ServiceRegistryConfig::InitRegistryConfig(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open register config file: {}", config_file);
        return false;
    }

    auto config = nlohmann::json::parse(file);

    std::string service_name = config.value("service_name", "");
    if (service_name.empty()) {
        LOG_ERROR("service_name is empty");
        return false;
    }
    SetServiceName(service_name);

    std::string service_version = config.value("service_version", "");
    if (service_version.empty()) {
        LOG_ERROR("service_version is empty");
        return false;
    }
    SetServiceVersion(service_version);

    if (!config.contains("registry_nodes")) {
        LOG_ERROR("registry_nodes is empty");
        return false;
    }

    std::vector<RegistryInfo> tmp_registry_nodes;
    for (const auto& node : config["registry_nodes"]) {
        RegistryInfo tmp_node;
        if (!node.contains("address") || !node.contains("port")) {
            return false;
        }
        tmp_node.address = node["address"];
        tmp_node.port = node["port"];
        tmp_registry_nodes.push_back(tmp_node);
    }
    std::swap(imp_->registry_nodes_, tmp_registry_nodes);
    return true;
}

void ServiceRegistryConfig::SetServiceName(const std::string& name)         { imp_->service_name_ = name; }
const std::string& ServiceRegistryConfig::GetServiceName() const            { return imp_->service_name_; }

void ServiceRegistryConfig::SetServiceVersion(const std::string& version)   { imp_->service_version_ = version; }
const std::string& ServiceRegistryConfig::GetServiceVersion() const         { return imp_->service_version_; }

const std::vector<RegistryInfo>& ServiceRegistryConfig::GetRegistryNodes() const { return imp_->registry_nodes_; }
size_t ServiceRegistryConfig::GetRegistryNodesSize() const                       { return imp_->registry_nodes_.size(); }

} // namespace cookrpc
