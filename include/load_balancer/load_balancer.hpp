#pragma once

#include <string>
#include <vector>
#include <memory>

namespace cookrpc {
    class LoadBalancer {
    public:
        virtual ~LoadBalancer() = default;

        virtual std::string select(const std::vector<std::string> &instances) = 0;
        static std::shared_ptr<LoadBalancer> getInstance();
        static bool initBalancer(const std::string &type = "random");
        static std::string selectServer(const std::vector<std::string> &servers);

    protected:
        LoadBalancer() noexcept = default;
    };
}
