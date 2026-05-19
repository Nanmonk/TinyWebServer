#include <gtest/gtest.h>
#include <ctime>
#include <vector>

#include "timer/lst_timer.h"

// ══════════════════════════════════════════════════════════════════
// 全局回调辅助（function pointer 不能捕获，用全局量记录）
// ══════════════════════════════════════════════════════════════════
static int g_cb_count = 0;
static int g_cb_ids[16]; // 记录触发顺序（借用 client_data.sockfd 作 ID）

static void count_cb(client_data *) { ++g_cb_count; }
static void record_cb(client_data *d) { g_cb_ids[g_cb_count++] = d->sockfd; }

// ══════════════════════════════════════════════════════════════════
// 测试夹具
// ══════════════════════════════════════════════════════════════════
class TimerListTest : public ::testing::Test {
protected:
    sort_timer_lst lst;
    std::vector<client_data> clients; // 管理 client_data 生命周期

    void SetUp() override {
        g_cb_count = 0;
        memset(g_cb_ids, 0, sizeof(g_cb_ids));
        // 预分配足够容量，防止 vector 扩容使 user_data 指针失效
        clients.reserve(16);
    }

    // 创建定时器；expire = time(now) + offset_sec
    // cb 默认 count_cb，id 用于 record_cb 时识别顺序
    util_timer *make_timer(int offset_sec,
                           void (*cb)(client_data *) = count_cb,
                           int id = 0) {
        clients.push_back({});
        clients.back().sockfd = id;
        clients.back().timer  = nullptr;

        auto *t      = new util_timer();
        t->expire    = time(nullptr) + offset_sec;
        t->cb_func   = cb;
        t->user_data = &clients.back();
        return t;
    }
};

// ══════════════════════════════════════════════════════════════════
// add_timer / del_timer：基本结构操作
// ══════════════════════════════════════════════════════════════════

TEST_F(TimerListTest, AddSingleTimer_ThenDelete_NoCrash) {
    util_timer *t = make_timer(60);
    lst.add_timer(t);
    lst.del_timer(t); // del_timer 内部 delete 对象
    lst.tick();       // 空链表 tick 不应崩溃
}

TEST_F(TimerListTest, DeleteHeadTimer_ListRemainsValid) {
    util_timer *t1 = make_timer(5);
    util_timer *t2 = make_timer(10);
    lst.add_timer(t1);
    lst.add_timer(t2);

    lst.del_timer(t1); // 删除头部
    // t2 仍在，tick 不应崩溃
    lst.tick();
    EXPECT_EQ(g_cb_count, 0); // t2 是未来时刻，不触发
    lst.del_timer(t2);
}

TEST_F(TimerListTest, DeleteTailTimer_ListRemainsValid) {
    util_timer *t1 = make_timer(5);
    util_timer *t2 = make_timer(10);
    lst.add_timer(t1);
    lst.add_timer(t2);

    lst.del_timer(t2); // 删除尾部
    lst.tick();
    EXPECT_EQ(g_cb_count, 0);
    lst.del_timer(t1);
}

TEST_F(TimerListTest, DeleteMiddleTimer_ListRemainsValid) {
    util_timer *t1 = make_timer(5);
    util_timer *t2 = make_timer(10);
    util_timer *t3 = make_timer(15);
    lst.add_timer(t1);
    lst.add_timer(t2);
    lst.add_timer(t3);

    lst.del_timer(t2); // 删除中间节点
    lst.tick();
    EXPECT_EQ(g_cb_count, 0);
    lst.del_timer(t1);
    lst.del_timer(t3);
}

// ══════════════════════════════════════════════════════════════════
// tick：过期触发 / 未来不触发
// ══════════════════════════════════════════════════════════════════

TEST_F(TimerListTest, Tick_FiresExpiredTimer) {
    util_timer *t = make_timer(-1); // 已过期（1 秒前）
    lst.add_timer(t);
    lst.tick(); // 应触发回调并 delete t
    EXPECT_EQ(g_cb_count, 1);
    // tick 之后不能再 del_timer(t)，因为它已被 delete
}

TEST_F(TimerListTest, Tick_SkipsFutureTimer) {
    util_timer *t = make_timer(3600); // 1 小时后
    lst.add_timer(t);
    lst.tick();
    EXPECT_EQ(g_cb_count, 0); // 不应触发
    lst.del_timer(t);
}

TEST_F(TimerListTest, Tick_FiresOnlyExpiredSubset) {
    // 2 个已过期 + 1 个未来
    util_timer *e1 = make_timer(-3, count_cb);
    util_timer *e2 = make_timer(-1, count_cb);
    util_timer *f1 = make_timer(3600, count_cb);

    lst.add_timer(e1);
    lst.add_timer(e2);
    lst.add_timer(f1);

    lst.tick();

    EXPECT_EQ(g_cb_count, 2); // 仅过期的两个触发
    lst.del_timer(f1);
}

// ══════════════════════════════════════════════════════════════════
// tick：触发顺序与链表排序
// ══════════════════════════════════════════════════════════════════

TEST_F(TimerListTest, AddTimers_SortedByExpire_TickFiresInOrder) {
    // 乱序添加，验证链表按 expire 升序排列
    // expire 最早的应最先触发
    util_timer *t3 = make_timer(-1, record_cb, 3); // expire = now-1（最晚过期）
    util_timer *t1 = make_timer(-3, record_cb, 1); // expire = now-3（最早过期）
    util_timer *t2 = make_timer(-2, record_cb, 2); // expire = now-2

    // 故意乱序添加
    lst.add_timer(t3);
    lst.add_timer(t1);
    lst.add_timer(t2);

    lst.tick(); // 应按 t1 → t2 → t3 顺序触发

    ASSERT_EQ(g_cb_count, 3);
    EXPECT_EQ(g_cb_ids[0], 1); // 最早过期的最先触发
    EXPECT_EQ(g_cb_ids[1], 2);
    EXPECT_EQ(g_cb_ids[2], 3);
}

// ══════════════════════════════════════════════════════════════════
// adjust_timer：延后到期时间后应重新排序
// ══════════════════════════════════════════════════════════════════

TEST_F(TimerListTest, AdjustTimer_MovesNodeToCorrectPosition) {
    // 初始链表：[t1(id=1, expire=now+5)] → [t2(id=2, expire=now+20)]
    util_timer *t1 = make_timer(5,  record_cb, 1);
    util_timer *t2 = make_timer(20, record_cb, 2);
    lst.add_timer(t1);
    lst.add_timer(t2);

    // 把 t1 的到期时间延后到 t2 之后
    t1->expire = time(nullptr) + 30;
    lst.adjust_timer(t1);
    // 链表应变为：[t2] → [t1]

    // 将两者都设为已过期，tick 按新顺序触发
    t2->expire = time(nullptr) - 2;
    t1->expire = time(nullptr) - 1;
    lst.tick();

    ASSERT_EQ(g_cb_count, 2);
    EXPECT_EQ(g_cb_ids[0], 2); // t2 现在是头部，先触发
    EXPECT_EQ(g_cb_ids[1], 1);
}

TEST_F(TimerListTest, AdjustTimer_Noop_WhenAlreadyInCorrectPosition) {
    util_timer *t1 = make_timer(5);
    util_timer *t2 = make_timer(10);
    lst.add_timer(t1);
    lst.add_timer(t2);
    // t1 到期早于 t2，adjust 是 no-op，不应崩溃
    lst.adjust_timer(t1);
    lst.del_timer(t1);
    lst.del_timer(t2);
}
