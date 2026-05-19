#include "log_manager.hpp"
#include "registry/services_registry.hpp"
#include "load_balancer/load_balancer.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    cookrpc::LoadBalancer::initBalancer("random");
    return 0;
}