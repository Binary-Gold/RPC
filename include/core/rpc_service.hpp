#pragma once

#include "proto/rpc_envelope.pb.h"
#include "serializer/protobuf_serializer.h"
#include "service/service.hpp"
#include "log_manager.hpp"

namespace cookrpc
{

    class RpcService : public Service
    {
    public:
        std::string GetServiceName() const override {
            return "RpcService";
        }

        bool HandleRequest(const std::string& method_name,
                           const std::string& args,
                           std::string& result) override
        {
            if (method_name == "Echo") {
                try {
                    minirpc::EchoRequest request;
                    if (!ProtobufSerializer::Deserialize(args, request)) {
                        LOG_ERROR("Failed to deserialize Echo request");
                        return false;
                    }

                    minirpc::EchoResponse response;
                    response.set_echo("hahah i am rpc server, welcome to C++ training camp, come on");
                    response.set_received_message(request.message());

                    if (!ProtobufSerializer::Serialize(response, result)) {
                        LOG_ERROR("Failed to serialize Echo response");
                        return false;
                    }
                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to process Echo request: {}", e.what());
                    return false;
                }
            }

            LOG_ERROR("Unknown method: {}", method_name);
            return false;
        }
    };

} // namespace cookrpc
