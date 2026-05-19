#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "load_balancer/load_balancer.hpp"

namespace cookrpc {
    class RoundLB : public LoadBalancer {
    public:
        RoundLB() : index_(0) {};
        std::string select(const std::vector<std::string> &instances) override {
            if (instances.empty()) {
                return "";
            }
            size_t idx = index_.fetch_add(1, std::memory_order_relaxed);
            return instances[idx % instances.size()];
        }

    private:
        std::atomic<size_t> index_;
    };
}