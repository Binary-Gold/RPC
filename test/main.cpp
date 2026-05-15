#include "log_manager.hpp"
#include "registry/services_registry.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    Logger::GetInstance().Init("../log");
    LOG_INFO("hello word!!");

    try {
        ServiceRegistry registry("127.0.0.1:2181");
        if (!registry.IsConnected()) {
            std::cerr << "fail to connect to zookeeper" << std::endl;
            return 1;
        }
        std::string service_name = "userService";
        std::string services_address = "192.168.1.100:8001";
        if (registry.RegisterService(service_name, services_address)) {
            std::cout << "RegisterService successfully" << std::endl;
        } else {
            std::cerr << "RegisterService failed" << std::endl;
            return 1;
        }

        while (true) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            if (!registry.IsConnected()) {
                std::cerr << "unconnected" << std::endl;
                break;
            }
            
            return 0;
        }
    } catch (...) {
        std::cerr << "ERROR" << std::endl;
        return 1;
    }
    return 0;
}