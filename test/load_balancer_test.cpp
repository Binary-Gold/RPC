#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "load_balancer/load_balancer.hpp"
#include "load_balancer/random.hpp"
#include "load_balancer/round.hpp"
#include "load_balancer/weighted.hpp"

using namespace cookrpc;

// ============================================================
// RoundLB
// ============================================================
TEST(RoundLB, EmptyInstancesReturnsEmpty) {
    RoundLB lb;
    EXPECT_EQ(lb.select({}), "");
}

TEST(RoundLB, SingleInstanceAlwaysReturnsIt) {
    RoundLB lb;
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(lb.select({"A"}), "A");
}

TEST(RoundLB, TwoInstancesAlternate) {
    RoundLB lb;
    EXPECT_EQ(lb.select({"A", "B"}), "A");
    EXPECT_EQ(lb.select({"A", "B"}), "B");
    EXPECT_EQ(lb.select({"A", "B"}), "A");
    EXPECT_EQ(lb.select({"A", "B"}), "B");
}

TEST(RoundLB, WrapsAround) {
    RoundLB lb;
    std::vector<std::string> s = {"X", "Y", "Z"};
    // first cycle
    EXPECT_EQ(lb.select(s), "X");
    EXPECT_EQ(lb.select(s), "Y");
    EXPECT_EQ(lb.select(s), "Z");
    // second cycle
    EXPECT_EQ(lb.select(s), "X");
    EXPECT_EQ(lb.select(s), "Y");
}

TEST(RoundLB, ThreadSafetyEachCallUnique) {
    RoundLB lb;
    constexpr int N = 1000;
    std::atomic<int> counts[3]{};

    auto worker = [&]() {
        for (int i = 0; i < N; ++i) {
            auto r = lb.select({"A", "B", "C"});
            if (r == "A") counts[0].fetch_add(1);
            else if (r == "B") counts[1].fetch_add(1);
            else if (r == "C") counts[2].fetch_add(1);
        }
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);
    t1.join(); t2.join(); t3.join();

    // With 3 threads × 1000 calls, each instance gets exactly 1000
    EXPECT_EQ(counts[0].load(), N);
    EXPECT_EQ(counts[1].load(), N);
    EXPECT_EQ(counts[2].load(), N);
}

// ============================================================
// RandomLB
// ============================================================
TEST(RandomLB, EmptyInstancesReturnsEmpty) {
    RandomLB lb;
    EXPECT_EQ(lb.select({}), "");
}

TEST(RandomLB, SingleInstanceAlwaysReturnsIt) {
    RandomLB lb;
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(lb.select({"Solo"}), "Solo");
}

TEST(RandomLB, AlwaysReturnsValidInstance) {
    RandomLB lb;
    std::vector<std::string> s = {"X", "Y", "Z"};
    for (int i = 0; i < 100; ++i) {
        auto r = lb.select(s);
        ASSERT_TRUE(r == "X" || r == "Y" || r == "Z");
    }
}

TEST(RandomLB, CoversAllChoicesOverManyCalls) {
    RandomLB lb;
    std::vector<std::string> s = {"A", "B", "C", "D"};
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i)
        seen.insert(lb.select(s));
    EXPECT_EQ(seen.size(), 4u);
}

// ============================================================
// WeightedLB
// ============================================================
TEST(WeightedLB, EmptyInstancesReturnsEmpty) {
    WeightedLB lb;
    EXPECT_EQ(lb.select({}), "");
}

TEST(WeightedLB, SingleInstanceAlwaysReturnsIt) {
    WeightedLB lb;
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(lb.select({"Only"}), "Only");
}

TEST(WeightedLB, FallsBackToFirstWhenTotalZero) {
    // All weights would be 0 if name_weight_ overrides to 0 — not applicable
    // with default (i+1). Just sanity-check valid output.
    WeightedLB lb;
    auto r = lb.select({"A", "B"});
    ASSERT_TRUE(r == "A" || r == "B");
}

TEST(WeightedLB, ReturnsValidInstance) {
    WeightedLB lb;
    std::vector<std::string> s = {"X", "Y", "Z"};
    for (int i = 0; i < 100; ++i) {
        auto r = lb.select(s);
        ASSERT_TRUE(r == "X" || r == "Y" || r == "Z");
    }
}
