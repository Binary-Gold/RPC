#pragma once

#include <atomic>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "load_balancer/load_balancer.hpp"

namespace cookrpc {
    class WeightedLB : public LoadBalancer {
    public:
        WeightedLB() {}

        // todo 平滑化
        std::string select(const std::vector<std::string> &instances) override {
            if (instances.empty())
                return {};

            size_t total = 0;
            std::vector<size_t> weights(instances.size());
            for (size_t i = 0; i < instances.size(); ++i) {
                size_t w = static_cast<size_t>(i + 1);
                weights[i] = w;
                total += w;
            }

            size_t pos = index_.fetch_add(1, std::memory_order_relaxed) % total;
            size_t acc = 0;
            for (size_t i = 0; i < instances.size(); ++i) {
                acc += weights[i];
                if (pos < acc)
                    return instances[i];
            }
            return instances[0];
        }

    private:
        std::atomic<size_t> index_{0};
        std::map<std::string, size_t> name_weight_;

        void setWeight_(const std::string& instance, size_t weight) {
            name_weight_[instance] = weight;
        }
    };
}
