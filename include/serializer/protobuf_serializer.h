#pragma once

#include <string>

#include <google/protobuf/message.h>

#include "log_manager.hpp"

namespace cookrpc {

class ProtobufSerializer {
public:
    template <typename T>
    static bool Serialize(const T& data, std::string& out) {
        static_assert(std::is_base_of_v<google::protobuf::Message, T>,
                      "T must be a protobuf message type");
        try {
            return data.SerializeToString(&out);
        } catch (const std::exception& e) {
            LOG_ERROR("Protobuf serialization error: {}", e.what());
            return false;
        }
    }

    template <typename T>
    static bool Deserialize(const std::string& buffer, T& data) {
        static_assert(std::is_base_of_v<google::protobuf::Message, T>,
                      "T must be a protobuf message type");
        try {
            return data.ParseFromString(buffer);
        } catch (const std::exception& e) {
            LOG_ERROR("Protobuf deserialization error: {}", e.what());
            return false;
        }
    }
};

} // namespace cookrpc
