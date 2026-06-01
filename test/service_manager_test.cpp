#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <future>

#include <nlohmann/json.hpp>

#include "core/rpc_service.hpp"
#include "service/service_manager.hpp"

using namespace cookrpc;

namespace {

class TestService : public Service {
public:
    explicit TestService(std::string name) : name_(std::move(name)) {}
    std::string GetServiceName() const override { return name_; }

    bool HandleRequest(const std::string& method, const std::string& args,
                       std::string& result) override {
        if (method == "Echo") {
            auto req = nlohmann::json::parse(args);
            nlohmann::json resp;
            resp["echo"] = req.value("msg", "");
            result = resp.dump();
            return true;
        }
        return false;
    }

private:
    std::string name_;
};

} // namespace

class ServiceManagerTest : public ::testing::Test {
protected:
    ServiceManager& mgr_ = ServiceManager::GetInstance();
};

// ============================================================
// 注册 + 同步调用
// ============================================================
TEST_F(ServiceManagerTest, RegisterAndCallEchoSync) {
    auto svc = std::make_shared<TestService>("EchoService");
    ASSERT_TRUE(mgr_.RegisterService(svc));

    nlohmann::json args;
    args["msg"] = "hello";

    std::string result;
    bool ok = mgr_.HandleRpcRequest("EchoService", "Echo", args.dump(), result);
    EXPECT_TRUE(ok);

    auto resp = nlohmann::json::parse(result);
    EXPECT_EQ(resp["echo"], "hello");
}

// ============================================================
// 异步调用
// ============================================================
TEST_F(ServiceManagerTest, RegisterAndCallEchoAsync) {
    auto svc = std::make_shared<TestService>("AsyncService");
    ASSERT_TRUE(mgr_.RegisterService(svc));

    meeting_ctrl::ThreadPoolSingleton::Init(2);

    nlohmann::json args;
    args["msg"] = "async_test";

    auto result = std::make_shared<std::string>();
    auto fut = mgr_.HandleRpcRequestAsync("AsyncService", "Echo", args.dump(), result);
    fut.wait();

    ASSERT_TRUE(fut.get());
    auto resp = nlohmann::json::parse(*result);
    EXPECT_EQ(resp["echo"], "async_test");
}

// ============================================================
// 错误路径
// ============================================================
TEST_F(ServiceManagerTest, UnknownServiceReturnsFalse) {
    std::string result;
    EXPECT_FALSE(mgr_.HandleRpcRequest("NoSuchService", "Echo", "{}", result));
}

TEST_F(ServiceManagerTest, UnknownMethodReturnsFalse) {
    auto svc = std::make_shared<TestService>("MethodTestService");
    ASSERT_TRUE(mgr_.RegisterService(svc));

    std::string result;
    EXPECT_FALSE(mgr_.HandleRpcRequest("MethodTestService", "NoSuchMethod", "{}", result));
}

TEST_F(ServiceManagerTest, RegisterNullServiceFails) {
    EXPECT_FALSE(mgr_.RegisterService(nullptr));
}

// ============================================================
// GetService / Register duplicate
// ============================================================
TEST_F(ServiceManagerTest, GetServiceReturnsRegistered) {
    auto svc = std::make_shared<TestService>("LookupService");
    ASSERT_TRUE(mgr_.RegisterService(svc));

    auto found = mgr_.GetService("LookupService");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->GetServiceName(), "LookupService");
}

TEST_F(ServiceManagerTest, RegisterDuplicateServiceFails) {
    auto svc1 = std::make_shared<TestService>("DuplicateService");
    auto svc2 = std::make_shared<TestService>("DuplicateService");

    ASSERT_TRUE(mgr_.RegisterService(svc1));
    EXPECT_FALSE(mgr_.RegisterService(svc2));
}
