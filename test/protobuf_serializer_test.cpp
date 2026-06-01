#include <gtest/gtest.h>

#include <string>

#include "proto/message.pb.h"
#include "serializer/protobuf_serializer.h"

using namespace cookrpc;

// ============================================================
// Serialize → Deserialize 往返
// ============================================================
TEST(ProtobufSerializerTest, RoundTripHelloRequest) {
    minirpc::HelloRequest req;
    req.set_name("world");

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buffer));
    EXPECT_GT(buffer.size(), 0u);

    minirpc::HelloRequest parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.name(), "world");
}

TEST(ProtobufSerializerTest, RoundTripHelloResponse) {
    minirpc::HelloResponse resp;
    resp.set_greeting("hello from server");

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(resp, buffer));

    minirpc::HelloResponse parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.greeting(), "hello from server");
}

TEST(ProtobufSerializerTest, RoundTripDefaultMessage) {
    minirpc::HelloRequest req;  // 默认空字符串

    std::string buffer;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buffer));

    minirpc::HelloRequest parsed;
    ASSERT_TRUE(ProtobufSerializer::Deserialize(buffer, parsed));
    EXPECT_EQ(parsed.name(), "");  // proto3 default
}

// ============================================================
// 错误路径
// ============================================================
TEST(ProtobufSerializerTest, DeserializeInvalidBufferFails) {
    std::string garbage = "not protobuf";
    minirpc::HelloRequest req;
    EXPECT_FALSE(ProtobufSerializer::Deserialize(garbage, req));
}

TEST(ProtobufSerializerTest, EmptyBufferParsesAsDefault) {
    // 空 buffer 是合法 proto（所有字段默认值）
    minirpc::HelloRequest req;
    EXPECT_TRUE(ProtobufSerializer::Deserialize("", req));
    EXPECT_EQ(req.name(), "");
}

// ============================================================
// 序列化输出非空
// ============================================================
TEST(ProtobufSerializerTest, NonEmptyMessageProducesNonEmptyBuffer) {
    minirpc::HelloRequest req;
    req.set_name("alice");
    std::string buf;
    ASSERT_TRUE(ProtobufSerializer::Serialize(req, buf));
    EXPECT_FALSE(buf.empty());
}
