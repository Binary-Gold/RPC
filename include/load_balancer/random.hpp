#pragma once

#include <random>
#include "load_balancer/load_balancer.hpp"

namespace cookrpc {
    class RandomLB : public LoadBalancer {
    public:
        RandomLB() : rd_(), gen_(rd_()) {};
        std::string select(const std::vector<std::string> &instances) override {
            if (instances.empty()) {
                return "";
            }

            std::uniform_int_distribution<> dis(0, instances.size() - 1);
            size_t index = dis(gen_);

            return instances[index];
        }
    
    private:
        std::random_device rd_;
        std::mt19937 gen_;
    };
}