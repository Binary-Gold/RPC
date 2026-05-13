#include "log_manager.hpp"

int main() {
    Logger::GetInstance().Init("../log");
    LOG_INFO("hello word!!");
}