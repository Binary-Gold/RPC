#include "core/rpc_client.hpp"
#include "core/error_code.hpp"
#include "proto/rpc_envelope.pb.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <future>

using namespace cookrpc;

void testSyncCall(RpcClient& client) {
    std::cout << "\n=== Testing Synchronous Call ===" << std::endl;

    minirpc::EchoRequest request;
    request.set_message("hello, i am rpc client");

    minirpc::EchoResponse response;
    bool success = client.Call<minirpc::EchoResponse, minirpc::EchoRequest>(
        "RpcService", "Echo", request, response);

    if (success) {
        std::cout << "Sync call succeeded, echo=" << response.echo()
                  << ", received=" << response.received_message() << std::endl;
    } else {
        std::cout << "Sync call failed" << std::endl;
    }
}

void testAsyncCall(RpcClient& client) {
    std::cout << "\n=== Testing Asynchronous Call ===" << std::endl;

    std::vector<std::future<minirpc::EchoResponse>> futures;

    for (int i = 0; i < 5; ++i) {
        minirpc::EchoRequest request;
        request.set_message("Hello from async client " + std::to_string(i));

        auto future = client.AsyncCall<minirpc::EchoResponse, minirpc::EchoRequest>(
            "RpcService", "Echo", request);

        futures.push_back(std::move(future));
        std::cout << "Async call " << i << " sent" << std::endl;
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            auto response = futures[i].get();
            std::cout << "Async call " << i << " succeeded, echo=" << response.echo() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Async call " << i << " failed: " << e.what() << std::endl;
        }
    }
}

int main() {
    try {
        RpcClient client;

        std::cout << "Connected to RPC server" << std::endl;

        testSyncCall(client);
        testAsyncCall(client);

        std::cout << "\nAll tests completed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
