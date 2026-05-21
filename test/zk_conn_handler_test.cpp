#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "load_balancer/zk_conn_handler.hpp"

using namespace cookrpc;

class ZkConnHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& h = ZkConnHandler::GetInstance();
        h.ResetForTesting();
    }

    void TearDown() override {
        auto& h = ZkConnHandler::GetInstance();
        h.ResetForTesting();
    }
};

TEST_F(ZkConnHandlerTest, InitWithValidConfig) {
    nlohmann::json config;
    config["zk_host"] = "127.0.0.1";
    config["zk_port"] = 2181;

    bool ok = ZkConnHandler::GetInstance().InitZkConnHandler(config);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ZkConnHandler::GetInstance().HasServiceRegistry());
}

TEST_F(ZkConnHandlerTest, InitWithEmptyConfigFails) {
    nlohmann::json config;
    bool ok = ZkConnHandler::GetInstance().InitZkConnHandler(config);
    EXPECT_FALSE(ok);
}

TEST_F(ZkConnHandlerTest, HasServiceRegistryFalseBeforeInit) {
    EXPECT_FALSE(ZkConnHandler::GetInstance().HasServiceRegistry());
}

TEST_F(ZkConnHandlerTest, GetServiceRegistryReturnsNullBeforeInit) {
    EXPECT_EQ(ZkConnHandler::GetInstance().GetServiceRegistry(), nullptr);
}

TEST_F(ZkConnHandlerTest, SetDefaultsThenInitWithoutExplicitConfig) {
    auto& h = ZkConnHandler::GetInstance();
    h.SetZkHost("127.0.0.1");
    h.SetZkPort(2181);

    ServiceRegistry* r = h.GetOrCreateServiceRegistry();
    EXPECT_NE(r, nullptr);
}

TEST_F(ZkConnHandlerTest, RegisterAndDiscoverServicesRoundTrip) {
    auto& h = ZkConnHandler::GetInstance();

    nlohmann::json config;
    config["zk_host"] = "127.0.0.1";
    config["zk_port"] = 2181;
    ASSERT_TRUE(h.InitZkConnHandler(config));

    EXPECT_TRUE(h.RegisterService("OrderService", "10.0.0.1:8080"));
    EXPECT_TRUE(h.RegisterService("OrderService", "10.0.0.2:8080"));

    auto instances = h.GetAllServices("OrderService");
    EXPECT_EQ(instances.size(), 2u);
}

TEST_F(ZkConnHandlerTest, GetServicesWithLoadBalancer) {
    auto& h = ZkConnHandler::GetInstance();

    nlohmann::json config;
    config["zk_host"] = "127.0.0.1";
    config["zk_port"] = 2181;
    ASSERT_TRUE(h.InitZkConnHandler(config));

    h.RegisterService("PaymentService", "10.0.0.1:9001");
    h.RegisterService("PaymentService", "10.0.0.2:9001");

    std::string server = h.GetServices("PaymentService");
    EXPECT_FALSE(server.empty());
}

TEST_F(ZkConnHandlerTest, GetOrCreateServiceRegistryReturnsSamePointer) {
    auto& h = ZkConnHandler::GetInstance();

    h.SetZkHost("127.0.0.1");
    h.SetZkPort(2181);

    ServiceRegistry* r1 = h.GetOrCreateServiceRegistry();
    ASSERT_NE(r1, nullptr);

    ServiceRegistry* r2 = h.GetOrCreateServiceRegistry();
    EXPECT_EQ(r1, r2);
}

TEST_F(ZkConnHandlerTest, UpdateServersFromZkRefreshesCache) {
    auto& h = ZkConnHandler::GetInstance();

    nlohmann::json config;
    config["zk_host"] = "127.0.0.1";
    config["zk_port"] = 2181;
    ASSERT_TRUE(h.InitZkConnHandler(config));

    h.RegisterService("CacheService", "10.0.0.1:7000");
    h.UpdateServersFromZk("CacheService");

    std::string server = h.GetServices("CacheService");
    EXPECT_FALSE(server.empty());
}
