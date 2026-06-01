#include <gtest/gtest.h>

#include <string>

#include "proto/rpc_envelope.pb.h"
#include "serializer/protobuf_serializer.h"

using namespace cookrpc;

// ============================================================
// Serialize → Deserialize 往返
// ============================================================
TEST(ProtobufSerializerTest, RoundTripRpcRequestProto) {
    minirpc::RpcRequestProto req;
    req.set_service_name("world");

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buffer));
    EXPECT_GT(buffer.size(), 0u);

    minirpc::RpcRequestProto parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.service_name(), "world");
}

TEST(ProtobufSerializerTest, RoundTripRpcResponseProto) {
    minirpc::RpcResponseProto resp;
    resp.set_error_message("hello from server");

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(resp, buffer));

    minirpc::RpcResponseProto parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.error_message(), "hello from server");
}

TEST(ProtobufSerializerTest, RoundTripDefaultMessage) {
    minirpc::RpcRequestProto req;  // 默认空字符串

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buffer));

    minirpc::RpcRequestProto parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.service_name(), "");  // proto3 default
}

// ============================================================
// 错误路径
// ============================================================
TEST(ProtobufSerializerTest, DeserializeInvalidBufferFails) {
    std::string garbage = "not protobuf";
    minirpc::RpcRequestProto req;
    EXPECT_FALSE(ProtobufSerializer::Deserialize(garbage, req));
}

TEST(ProtobufSerializerTest, EmptyBufferParsesAsDefault) {
    // 空 buffer 是合法 proto（所有字段默认值）
    minirpc::RpcRequestProto req;
    EXPECT_TRUE(ProtobufSerializer::Deserialize("", req));
    EXPECT_EQ(req.service_name(), "");
}

// ============================================================
// 序列化输出非空
// ============================================================
TEST(ProtobufSerializerTest, NonEmptyMessageProducesNonEmptyBuffer) {
    minirpc::RpcRequestProto req;
    req.set_service_name("alice");
    std::string buf;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buf));
    EXPECT_FALSE(buf.empty());
}
