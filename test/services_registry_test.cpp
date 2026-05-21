#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "registry/services_registry.hpp"

class ServiceRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = std::make_unique<ServiceRegistry>("127.0.0.1:2181");
    }

    void TearDown() override {
        registry_.reset();
    }

    std::unique_ptr<ServiceRegistry> registry_;
};


TEST_F(ServiceRegistryTest, RegisterServiceSucceeds) {
    bool ok = registry_->RegisterService("test_service", "192.168.1.100:9001");
    EXPECT_TRUE(ok);
}

TEST_F(ServiceRegistryTest, DiscoverServicesReturnsRegisteredInstances) {
    registry_->RegisterService("UserService", "10.0.0.1:8080");
    registry_->RegisterService("UserService", "10.0.0.2:8080");

    auto instances = registry_->DiscoverServices("UserService");
    EXPECT_EQ(instances.size(), 2u);
}

TEST_F(ServiceRegistryTest, DiscoverServicesEmptyWhenNotFound) {
    auto instances = registry_->DiscoverServices("NonExistentService");
    EXPECT_TRUE(instances.empty());
}

TEST_F(ServiceRegistryTest, RegisterSameAddressTwiceIsIdempotent) {
    EXPECT_TRUE(registry_->RegisterService("IdempotentService", "10.0.0.1:8080"));
    EXPECT_TRUE(registry_->RegisterService("IdempotentService", "10.0.0.1:8080"));

    auto instances = registry_->DiscoverServices("IdempotentService");
    EXPECT_EQ(instances.size(), 1u);
}

TEST_F(ServiceRegistryTest, EphemeralNodesDisappearAfterDestruction) {
    {
        auto temp = std::make_unique<ServiceRegistry>("127.0.0.1:2181");
        temp->RegisterService("EphemeralTest", "10.0.0.1:9999");

        auto instances = temp->DiscoverServices("EphemeralTest");
        ASSERT_EQ(instances.size(), 1u);
    }
    // temp 析构 → zookeeper_close → 临时节点自动删除

    // 用同一个连接验证临时节点已消失
    auto instances = registry_->DiscoverServices("EphemeralTest");
    EXPECT_TRUE(instances.empty());
}

TEST_F(ServiceRegistryTest, MultipleServicesDoNotInterfere) {
    registry_->RegisterService("ServiceA", "10.0.0.1:8080");
    registry_->RegisterService("ServiceB", "10.0.0.1:9090");

    EXPECT_EQ(registry_->DiscoverServices("ServiceA").size(), 1u);
    EXPECT_EQ(registry_->DiscoverServices("ServiceB").size(), 1u);
}
