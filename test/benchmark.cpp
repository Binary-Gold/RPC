#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>

#include "core/rpc_client.hpp"
#include "proto/rpc_envelope.pb.h"
#include "load_config/rpc_client_config.hpp"

using namespace cookrpc;
using namespace std::chrono;

struct BenchmarkResult {
    std::atomic<uint64_t> success_count{0};
    std::atomic<uint64_t> fail_count{0};
    std::atomic<uint64_t> total_latency_us{0};
};

// 阶段二：计时压测 —— 异步流水线
void worker(RpcClient& client, int client_id, int requests_per_client, size_t payload_size, BenchmarkResult& result) {
    // 逐条同步发，每次真实计时
    for (int i = 0; i < requests_per_client; ++i) {
        minirpc::EchoRequest req;
        req.set_message(std::string(payload_size, 'X'));

        auto t1 = high_resolution_clock::now();

        minirpc::EchoResponse resp;
        bool ok = client.Call<minirpc::EchoResponse, minirpc::EchoRequest>(
            "RpcService", "Echo", req, resp);

        auto t2 = high_resolution_clock::now();
        auto latency_us = duration_cast<microseconds>(t2 - t1).count();
        result.total_latency_us.fetch_add(latency_us);

        if (ok) result.success_count.fetch_add(1);
        else    result.fail_count.fetch_add(1);
    }
}

int main(int argc, char* argv[]) {
    int num_clients = 50;
    int requests_per_client = 100;
    size_t payload_size = 32;
    if (argc >= 2) num_clients = std::stoi(argv[1]);
    if (argc >= 3) requests_per_client = std::stoi(argv[2]);
    if (argc >= 4) payload_size = std::stoul(argv[3]);

    int total_reqs = num_clients * requests_per_client;
    std::cout << "\n========== RPC Benchmark ==========\n";
    std::cout << "Clients:      " << num_clients << "\n";
    std::cout << "Requests/cli: " << requests_per_client << "\n";
    std::cout << "Total reqs:   " << total_reqs << "\n";
    std::cout << "Payload:      " << payload_size << " bytes\n";
    std::cout << "====================================\n\n";

    // 阶段零：初始化配置一次
    RpcClientConfig::GetInstance().InitRpcClientConfig("config/rpc_client.json");

    // 阶段一：全量建连（不计时）
    std::cout << "Connecting " << num_clients << " clients..." << std::endl;
    std::vector<std::unique_ptr<RpcClient>> clients;
    clients.reserve(num_clients);
    for (int i = 0; i < num_clients; ++i) {
        clients.push_back(std::make_unique<RpcClient>());
    }
    std::cout << "All clients connected.\n" << std::endl;

    // 阶段二：压测（计时）
    BenchmarkResult result;
    auto bench_start = high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_clients);
    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back(worker, std::ref(*clients[i]), i, requests_per_client, payload_size, std::ref(result));
    }
    for (auto& t : threads) t.join();

    auto bench_end = high_resolution_clock::now();
    double elapsed_s = duration_cast<microseconds>(bench_end - bench_start).count() / 1'000'000.0;

    uint64_t success = result.success_count.load();
    uint64_t failed = result.fail_count.load();
    uint64_t total = success + failed;
    double avg_latency_us = (total > 0) ? (double)result.total_latency_us.load() / total : 0;
    double qps = (elapsed_s > 0) ? total / elapsed_s : 0;

    std::cout << "\n========== Results ==========\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total requests:  " << total << "\n";
    std::cout << "Success rate:    " << (total > 0 ? success * 100.0 / total : 0) << "%\n";
    std::cout << "Elapsed:         " << elapsed_s << " s\n";
    std::cout << "QPS:             " << qps << "\n";
    std::cout << "Avg latency:     " << avg_latency_us << " us\n";
    std::cout << "=============================\n";

    return (failed == 0) ? 0 : 1;
}
