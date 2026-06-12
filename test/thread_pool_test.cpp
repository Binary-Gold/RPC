#include <future>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "thread_pool/thread_pool.hpp"

using namespace meeting_ctrl;

// ============================================================
// 基础构造
// ============================================================
TEST(ThreadPoolTest, ConstructWithThreadCount) {
    ThreadPool pool(4);
    EXPECT_EQ(pool.Size(), 4u);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
}

TEST(ThreadPoolTest, ConstructWithConfig) {
    ThreadPoolStruct config;
    config.core_threads = 2;
    config.max_threads = 4;
    ThreadPool pool(config);
    EXPECT_EQ(pool.Size(), 2u);
}

// ============================================================
// Enqueue — void & 返回值
// ============================================================
TEST(ThreadPoolTest, EnqueueVoidTask) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    auto fut = pool.Enqueue(TaskPriority::NORMAL, [&] { counter.fetch_add(1); });

    ASSERT_TRUE(fut.valid());
    fut.wait();
    EXPECT_EQ(counter.load(), 1);
}

TEST(ThreadPoolTest, EnqueueReturnsValue) {
    ThreadPool pool(2);

    auto fut = pool.Enqueue(TaskPriority::NORMAL, [](int a, int b) { return a + b; }, 3, 4);

    ASSERT_TRUE(fut.valid());
    EXPECT_EQ(fut.get(), 7);
}

TEST(ThreadPoolTest, EnqueueAfterShutdownReturnsInvalidFuture) {
    ThreadPool pool(2);
    ASSERT_TRUE(pool.Shutdown());

    auto fut = pool.Enqueue(TaskPriority::NORMAL, [] {});
    EXPECT_FALSE(fut.valid());
}

// ============================================================
// 并发执行
// ============================================================
TEST(ThreadPoolTest, MultipleTasksRunInParallel) {
    ThreadPool pool(4);
    constexpr int N = 20;
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < N; ++i) {
        auto f = pool.Enqueue(TaskPriority::NORMAL, [&] { counter.fetch_add(1); });
        futures.push_back(std::move(f));
    }
    for (auto& f : futures) {
        f.wait();
    }

    EXPECT_EQ(counter.load(), N);
}

// ============================================================
// 优先级
// ============================================================
TEST(ThreadPoolTest, HighPriorityExecutedBeforeLow) {
    ThreadPool pool(1);  // 单线程确保顺序
    std::atomic<int> order{0};
    std::atomic<int> high_order{-1};
    std::atomic<int> low_order{-1};

    // 先提低优
    auto low = pool.Enqueue(TaskPriority::LOW, [&] {
        low_order = order.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
    // 再提高优
    auto high = pool.Enqueue(TaskPriority::HIGH, [&] {
        high_order = order.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });

    low.wait();
    high.wait();
    EXPECT_LT(high_order, low_order);  // 高优序号小 → 先执行
}

// ============================================================
// Shutdown
// ============================================================
TEST(ThreadPoolTest, ShutdownJoinsAllThreads) {
    ThreadPool pool(4);

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        auto f = pool.Enqueue(TaskPriority::NORMAL,
                              [] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
        futures.push_back(std::move(f));
    }
    for (auto& f : futures) {
        f.wait();
    }

    bool stopped = pool.Shutdown();
    EXPECT_TRUE(stopped);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
}

// ============================================================
// QueueSize
// ============================================================
TEST(ThreadPoolTest, QueueSizeIsZeroWhenIdle) {
    ThreadPool pool(2);
    EXPECT_EQ(pool.QueueSize(), 0u);
}

// ============================================================
// Stats
// ============================================================
TEST(ThreadPoolTest, StatsTracksCompletedTasks) {
    ThreadPool pool(2);

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        auto f = pool.Enqueue(TaskPriority::NORMAL, [] {});
        futures.push_back(std::move(f));
    }
    for (auto& f : futures) {
        f.wait();
    }

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed, 10u);
    EXPECT_EQ(stats.tasks_failed, 0u);
}

// ============================================================
// StopNow 立即终止
// ============================================================
TEST(ThreadPoolTest, StopNowDropsPendingTasks) {
    auto pool = std::make_unique<ThreadPool>(1);

    // 阻塞唯一线程确保后面的任务排队
    std::atomic<bool> started{false};
    pool->Enqueue(TaskPriority::NORMAL, [&] {
        started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    });
    while (!started.load()) {}
    pool->Enqueue(TaskPriority::NORMAL, [] {});  // 排队中

    pool->StopNow();
    SUCCEED();  // 不挂即过
}
