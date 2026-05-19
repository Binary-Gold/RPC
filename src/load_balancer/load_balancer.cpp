#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "load_balancer/load_balancer.hpp"
#include "load_balancer/random.hpp"
#include "load_balancer/round.hpp"
#include "load_balancer/weighted.hpp"
#include "log_manager.hpp"

namespace cookrpc {
    namespace  {
        struct BalancerFactory {
            static std::shared_ptr<LoadBalancer> createRandom()
            {
                return std::make_shared<RandomLB>();
            }

            static std::shared_ptr<LoadBalancer> createRoundRobin()
            {
                return std::make_shared<RoundLB>();
            }

            static std::shared_ptr<LoadBalancer> createWeighted()
            {
                return std::make_shared<WeightedLB>();
            }

            static const std::unordered_map<std::string, std::function<std::shared_ptr<LoadBalancer>()>> &getMap()
            {
                static const std::unordered_map<std::string,
                                                std::function<std::shared_ptr<LoadBalancer>()>>
                    map = {
                        {"random", createRandom},
                        {"round", createRoundRobin},
                        {"weight", createWeighted}};
                return map;
            }
        };
    }

    struct Imp {
        inline static std::shared_ptr<LoadBalancer> instance_{nullptr};
        inline static std::mutex mtx_;
    };

    std::shared_ptr<LoadBalancer> LoadBalancer::getInstance() {
        if (!Imp::instance_) {
            std::lock_guard<std::mutex> lock(Imp::mtx_);
            if (!Imp::instance_) {
                Imp::instance_ = BalancerFactory::createRandom();
            }
        }
        return Imp::instance_;
    }

    bool LoadBalancer::initBalancer(const std::string& type) {
        std::lock_guard<std::mutex> lock(Imp::mtx_);
        const auto &balancer_map = BalancerFactory::getMap();
        auto it = balancer_map.find(type);

        if (it != balancer_map.end()) {
            try {   
                Imp::instance_ = it->second();
                return true;
            } catch (const std::exception &e) {
                LOG_ERROR("Failed to initialize {} load balancer: {}", type, e.what());
                // 如果查询失败，则使用随机负载均衡器
                Imp::instance_ = BalancerFactory::createRandom();
                return false;
            }
        }

        LOG_WARN("Unknown load balancer type: {}, falling back to random", type);
        Imp::instance_ = BalancerFactory::createRandom();
        return false;
    }

    std::string LoadBalancer::selectServer(const std::vector<std::string>& servers) {
        if (!Imp::instance_) {
            std::lock_guard<std::mutex> lock(Imp::mtx_);
            if (!Imp::instance_) {
                LOG_WARN("Load balancer not initialized, using default random balancer");
                Imp::instance_ = BalancerFactory::createRandom();
            }
        }

        if (servers.empty()) {
            LOG_ERROR("No instances available for load balancing");
            return "";
        }

        try
        {
            return Imp::instance_->select(servers);
        } 
        catch (const std::exception &e)
        {
            LOG_ERROR("Error in load balancer select: {}", e.what());
            return servers[0];
        }
    }
}