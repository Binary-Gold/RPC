#include "protocol/rpc_protocol.hpp"
#include "proto/rpc_envelope.pb.h"
#include "serializer/protobuf_serializer.h"

namespace cookrpc {

bool RpcRequest::Serialize(std::string& out) const {
    minirpc::RpcRequestProto env;
    env.set_service_name(service_name_);
    env.set_method_name(method_name_);
    env.set_sequence_id(sequence_id_);
    env.set_payload(payload_);
    return ProtobufSerializer::Serialize(env, out);
}

bool RpcRequest::Deserialize(const std::string& in) {
    minirpc::RpcRequestProto env;
    if (!ProtobufSerializer::Deserialize(in, env)) {
        return false;
    }
    service_name_ = env.service_name();
    method_name_  = env.method_name();
    sequence_id_  = env.sequence_id();
    payload_      = env.payload();
    return true;
}

bool RpcResponse::Serialize(std::string& out) const {
    minirpc::RpcResponseProto env;
    env.set_error_code(error_code_);
    env.set_error_message(error_message_);
    env.set_result_data(result_data_);
    env.set_sequence_id(sequence_id_);
    return ProtobufSerializer::Serialize(env, out);
}

bool RpcResponse::Deserialize(const std::string& in) {
    minirpc::RpcResponseProto env;
    if (!ProtobufSerializer::Deserialize(in, env)) {
        return false;
    }
    error_code_    = env.error_code();
    error_message_ = env.error_message();
    result_data_   = env.result_data();
    sequence_id_   = env.sequence_id();
    return true;
}

}
