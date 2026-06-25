#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

// 1. 非激活（active=false）→ 不注入，模式跟随 hos 但不启动 burst
void test_inactive_no_burst()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(13, 100, false); // NAG but inactive
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(0, n.reactiveBursts());
}

// 2. REACTIVE：active + hos>=3 → 立即 hold，模式 REACTIVE
void test_reactive_starts_hold()
{
    DashReactiveNagBurst n;
    n.init(1);
    n.onNagSample(5, 100, true);
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(1, n.reactiveBursts());
}

// 3. 持续几何（核心，区别 v1）：REACTIVE 多帧 computeHold 全部 > 0（积分>0）
void test_reactive_hold_positive_integral()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(5, 0, true);
    int sum = 0, mn = 1 << 30;
    for (unsigned long t = 0; t < 1200; t += 20) // 1.2s, ~50Hz
    {
        if (!n.shouldEcho(t)) break;
        int p = n.computeHold(t);
        if (p < mn) mn = p;
        sum += p;
    }
    TEST_ASSERT_TRUE(mn > 0);  // 全正（持续 hold，非零均值）
    TEST_ASSERT_TRUE(sum > 0); // 积分 > 0
}

// 4. PROACTIVE：active + hos<=2 → 周期性轻 hold
void test_proactive_periodic_hold()
{
    DashReactiveNagBurst n;
    n.init(3);
    n.onNagSample(0, 100, true); // 无 NAG, active
    TEST_ASSERT_EQUAL(NagMode::PROACTIVE, n.mode());
    // 首次进入 PROACTIVE 立即排一次（nextProactiveMs=now）
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL(1, n.proactiveWiggles());
}

// 5. NAG 清除全复位（修 rec8）：hos>=3 → <=2 → reactive 状态清，下段 NAG 立即爆发
void test_nag_clear_resets_no_stale_cooldown()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.onNagSample(5, 100, true); // burst 1
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    for (unsigned long t = 100; n.shouldEcho(t); t += 500)
        n.computeHold(t);                            // drain (advance time)
    unsigned long after = n.lastReactiveEndMs + 200; // coherent time after burst end
    n.onNagSample(0, after, true);                   // NAG clear → full reset
    TEST_ASSERT_EQUAL(NagMode::PROACTIVE, n.mode());
    // 紧接新 NAG（无 3s 冷却挡）
    n.onNagSample(5, after + 100, true);
    TEST_ASSERT_TRUE(n.shouldEcho(after + 100)); // 立即爆发，无陈旧冷却
    TEST_ASSERT_EQUAL(2, n.reactiveBursts());
}

// 6. NAG 期内 ~800ms 间隔（不超密）
void test_reactive_intra_episode_gap()
{
    DashReactiveNagBurst n;
    n.init(5);
    n.onNagSample(5, 100, true); // burst 1
    for (unsigned long t = 100; n.shouldEcho(t); t += 500)
        n.computeHold(t);                  // drain (advance time)
    unsigned long E = n.lastReactiveEndMs; // burst end time
    n.onNagSample(5, E + 300, true);       // gap 300ms < 800 → no new burst
    TEST_ASSERT_FALSE(n.shouldEcho(E + 300));
    n.onNagSample(5, E + 900, true); // gap 900ms > 800 → burst 2
    TEST_ASSERT_TRUE(n.shouldEcho(E + 900));
    TEST_ASSERT_EQUAL(2, n.reactiveBursts());
}

// 7. 模式切换：proactive 进行中 NAG 起 → 切 REACTIVE
void test_proactive_interrupted_by_nag()
{
    DashReactiveNagBurst n;
    n.init(6);
    n.onNagSample(0, 100, true); // proactive
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    n.onNagSample(5, 120, true); // NAG → REACTIVE (interrupts proactive)
    TEST_ASSERT_EQUAL(NagMode::REACTIVE, n.mode());
}

// 8. applyToFrame 加 human_weight + pert
void test_apply_to_frame_adds_human_weight_and_pert()
{
    DashReactiveNagBurst n;
    n.init(7);
    uint8_t d2lo = 0x08, d3 = 0x12; // base 0x0812
    n.applyToFrame(d2lo, d3, 70);   // +human_weight 8 + pert 70 = +78
    int out = (int)(((uint16_t)d2lo << 8) | d3);
    TEST_ASSERT_EQUAL_INT16(0x0812 + 8 + 70, out);
}

// 9. 峰值 cap：computeHold 不超 kAmplitudeCap
void test_hold_respects_cap()
{
    DashReactiveNagBurst n;
    n.init(8);
    n.onNagSample(5, 0, true);
    for (unsigned long t = 0; t < 1200; t += 20)
    {
        if (!n.shouldEcho(t)) break;
        int p = n.computeHold(t);
        TEST_ASSERT_TRUE(p <= n.amplitudeCap());
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_inactive_no_burst);
    RUN_TEST(test_reactive_starts_hold);
    RUN_TEST(test_reactive_hold_positive_integral);
    RUN_TEST(test_proactive_periodic_hold);
    RUN_TEST(test_nag_clear_resets_no_stale_cooldown);
    RUN_TEST(test_reactive_intra_episode_gap);
    RUN_TEST(test_proactive_interrupted_by_nag);
    RUN_TEST(test_apply_to_frame_adds_human_weight_and_pert);
    RUN_TEST(test_hold_respects_cap);
    return UNITY_END();
}
