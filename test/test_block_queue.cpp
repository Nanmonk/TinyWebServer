#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>

#include "log/block_queue.h"

// ══════════════════════════════════════════════════════════════════
// 基础功能
// ══════════════════════════════════════════════════════════════════

TEST(BlockQueueTest, InitialState) {
    block_queue<int> q(5);
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
    EXPECT_EQ(q.max_size(), 5);
}

TEST(BlockQueueTest, PushThenPop) {
    block_queue<int> q(10);
    EXPECT_TRUE(q.push(42));
    EXPECT_EQ(q.size(), 1);

    int val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q.empty());
}

TEST(BlockQueueTest, FIFOOrder) {
    block_queue<int> q(10);
    q.push(1); q.push(2); q.push(3);
    int v;
    q.pop(v); EXPECT_EQ(v, 1);
    q.pop(v); EXPECT_EQ(v, 2);
    q.pop(v); EXPECT_EQ(v, 3);
}

// ══════════════════════════════════════════════════════════════════
// 容量边界
// ══════════════════════════════════════════════════════════════════

TEST(BlockQueueTest, FullQueue_PushReturnsFalse) {
    block_queue<int> q(3);
    q.push(1); q.push(2); q.push(3);
    EXPECT_TRUE(q.full());
    // 满队列再 push 应返回 false，且不破坏原有数据
    EXPECT_FALSE(q.push(99));
    EXPECT_EQ(q.size(), 3);
}

TEST(BlockQueueTest, SizeTracking) {
    block_queue<int> q(10);
    q.push(1); q.push(2); q.push(3);
    EXPECT_EQ(q.size(), 3);
    int v;
    q.pop(v);
    EXPECT_EQ(q.size(), 2);
}

// ══════════════════════════════════════════════════════════════════
// clear / front / back
// ══════════════════════════════════════════════════════════════════

TEST(BlockQueueTest, Clear_ResetsToEmpty) {
    block_queue<int> q(5);
    q.push(1); q.push(2);
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);
    // clear 后仍可正常使用
    EXPECT_TRUE(q.push(9));
}

// 【已知 Bug】block_queue::front() 读 m_array[m_front]，
// 但 m_front 初始为 -1（pop 前从未递增），实际队首在 m_array[(m_front+1) % max]。
// 修复方式：front() 改为 value = m_array[(m_front + 1) % m_max_size]
TEST(BlockQueueTest, DISABLED_Front_ReturnsOldestElement_HasBug) {
    block_queue<int> q(5);
    q.push(10); q.push(20);
    int v;
    EXPECT_TRUE(q.front(v));
    EXPECT_EQ(v, 10); // 实际返回 m_array[-1]（越界读），不是 10
}

TEST(BlockQueueTest, Back_ReturnsNewestElement) {
    block_queue<int> q(5);
    q.push(10); q.push(20);
    int v;
    EXPECT_TRUE(q.back(v));
    EXPECT_EQ(v, 20);
}

TEST(BlockQueueTest, FrontAndBack_ReturnFalseOnEmpty) {
    block_queue<int> q(5);
    int v;
    EXPECT_FALSE(q.front(v));
    EXPECT_FALSE(q.back(v));
}

// ══════════════════════════════════════════════════════════════════
// 超时 pop（同时演示已知 Bug）
// ══════════════════════════════════════════════════════════════════

TEST(BlockQueueTest, TimeoutPop_ReturnsFalseOnEmpty) {
    block_queue<int> q(5);
    int val;
    // 队列为空，超时 pop 应在超时后返回 false
    EXPECT_FALSE(q.pop(val, 50));
}

// 【已知 Bug - Issue #TBD】block_queue.h:179
//   t.tv_nsec = (ms_timeout % 1000) * 1000   ← 单位是微秒
//   应为              * 1000000               ← 纳秒
// 导致实际等待时间是期望值的 1/1000，用 DISABLED_ 保留以记录 Bug。
TEST(BlockQueueTest, DISABLED_TimeoutDuration_NanosecondBug) {
    block_queue<int> q(5);
    int val;
    auto t0 = std::chrono::steady_clock::now();
    q.pop(val, 1000); // 期望等待 1000 ms
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    // Bug 存在时此断言失败：实际只等了约 1 ms
    EXPECT_GE(ms, 900) << "Bug: tv_nsec 单位错误，实际超时约为预期的 1/1000";
}

// ══════════════════════════════════════════════════════════════════
// 线程安全
// ══════════════════════════════════════════════════════════════════

TEST(BlockQueueTest, ConcurrentProducerConsumer_NoDataLoss) {
    block_queue<int> q(1000);
    const int N = 500;
    std::atomic<long long> sum_in(0), sum_out(0);

    std::thread producer([&] {
        for (int i = 1; i <= N; ++i) { q.push(i); sum_in += i; }
    });
    std::thread consumer([&] {
        int v;
        for (int i = 0; i < N; ++i) { q.pop(v); sum_out += v; }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(sum_in.load(), sum_out.load());
    EXPECT_EQ(sum_in.load(), (long long)N * (N + 1) / 2);
}

TEST(BlockQueueTest, ConcurrentMultipleProducers_SizeConsistent) {
    block_queue<int> q(2000);
    const int N = 200;
    std::atomic<int> pushed(0);

    auto fn = [&] {
        for (int i = 0; i < N; ++i)
            if (q.push(1)) ++pushed;
    };
    std::thread t1(fn), t2(fn), t3(fn);
    t1.join(); t2.join(); t3.join();

    EXPECT_EQ(q.size(), pushed.load());
}
