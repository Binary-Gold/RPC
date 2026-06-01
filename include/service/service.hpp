#pragma once
#include <string>

namespace cookrpc
{

    class Service
    {
    public:
        virtual ~Service() = default;

        // 获取服务名称
        virtual std::string GetServiceName() const = 0;

        // 处理请求
        virtual bool HandleRequest(const std::string &method_name,
                                   const std::string &args,
                                   std::string &result) = 0;
    };

} // namespace cookrpc