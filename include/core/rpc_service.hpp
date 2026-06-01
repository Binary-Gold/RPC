#pragma once
#include <nlohmann/json.hpp>

#include "service/service.hpp"
#include "log_manager.hpp"

namespace cookrpc
{

    class RpcService : public Service
    {
    public:
        std::string GetServiceName() const override
        {
            return "RpcService";
        }

        bool HandleRequest(const std::string &method_name,
                           const std::string &args,
                           std::string &result) override
        {
            if (method_name == "Echo")
            {
                try
                {
                    nlohmann::json request = nlohmann::json::parse(args);
                    
                    // 创建JSON响应
                    nlohmann::json response;
                    response["echo"] = "hahah i am rpc server, welcome to C++ training camp, come on";
                    response["received_message"] = request.value("message", "");
                    
                    // 序列化为字符串返回
                    result = response.dump();
                    return true;
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Failed to process Echo request: {}", e.what());
                    
                    // 返回错误的JSON响应
                    nlohmann::json error_response;
                    error_response["error"] = "Invalid request format";
                    result = error_response.dump();
                    return false;
                }
            }

            LOG_ERROR("Unknown method: {}", method_name);
            return false;
        }
    };

} // namespace cookrpc