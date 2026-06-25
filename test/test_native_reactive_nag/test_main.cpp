#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

// 1. NAG 未触发（hands_on_state<=2）→ 不注入
void test_no_nag_no_inject()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(2, 100);
    TEST_ASSERT_FALSE(n.shouldInject(100));
    TEST_ASSERT_FALSE(n.isNagActive());
}

// 2. NAG 触发（hands_on_state>=3）→ 启动爆发，应注入
void test_nag_starts_burst()
{
    DashReactiveNagBurst n;
    n.init(1);
    n.onNagSample(13, 100); // Jordan 实测 NAG 值 12,13
    TEST_ASSERT_TRUE(n.isNagActive());
    TEST_ASSERT_TRUE(n.shouldInject(100));
    TEST_ASSERT_EQUAL(1, n.burstsThisCycle());
}

// 3. 第二次爆发需 1500ms 间隔（burst gap）
void test_burst_gap_enforced()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(13, 100); // burst #1, lastBurstMs=100
    TEST_ASSERT_TRUE(n.shouldInject(100));
    // 推进到爆发结束（stroke 完成）→ injecting=false。
    // 必须递增 nowMs：computeWave 在 elapsed>strokeDur 时推进一个 stroke 并把
    // waveStartMs 重置为 nowMs，故每步 +500ms（>strokeDur 400）才推进。
    unsigned long t = 100;
    while (n.shouldInject(t))
    {
        n.computeWave(t);
        t += 500;
    }
    n.onNagSample(13, 200); // 距上次 100ms < 1500 → 不启动新爆发
    TEST_ASSERT_FALSE(n.shouldInject(200));
    n.onNagSample(13, 1700); // 距上次 1600ms > 1500 → 启动 #2
    TEST_ASSERT_TRUE(n.shouldInject(1700));
    TEST_ASSERT_EQUAL(2, n.burstsThisCycle());
}

// 4. 3 爆发后强制 3s 冷却
void test_cooldown_after_three_bursts()
{
    DashReactiveNagBurst n;
    n.init(3);
    unsigned long t = 100;
    for (int burst = 0; burst < 3; ++burst)
    {
        n.onNagSample(13, t);
        TEST_ASSERT_TRUE(n.shouldInject(t));
        unsigned long dt = t; // 递增 drain（同上）
        while (n.shouldInject(dt))
        {
            n.computeWave(dt);
            dt += 500;
        }
        t += 1600; // 下一爆发 gap
    }
    TEST_ASSERT_EQUAL(3, n.burstsThisCycle());
    n.onNagSample(13, t); // 第 4 次 → 触发冷却
    TEST_ASSERT_FALSE(n.shouldInject(t));
    TEST_ASSERT_TRUE(n.cooldownRemainingMs(t) > 0);
    n.onNagSample(13, t + 3100); // 冷却过后 → 可再爆发
    TEST_ASSERT_TRUE(n.shouldInject(t + 3100));
}

// 5. computeWave 半正弦：非零出现，且不超 cap
void test_wave_half_sine_shape()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.onNagSample(13, 0);
    int first = n.computeWave(0);
    int mid = n.computeWave(175);
    int maxSeen = (abs(first) > abs(mid)) ? abs(first) : abs(mid);
    for (unsigned long t = 176; t <= 400; ++t)
    {
        int w = n.computeWave(t);
        if (abs(w) > maxSeen) maxSeen = abs(w);
        TEST_ASSERT_TRUE(abs(w) <= n.amplitudeCap());
    }
    TEST_ASSERT_TRUE(maxSeen > 0);
    TEST_ASSERT_TRUE(maxSeen <= n.amplitudeCap());
}

// 6. 幅度分级：hands_on_state==3 轻（60~75）；>3 重（80~95）；不超 cap
void test_amplitude_tiers_and_cap()
{
    DashReactiveNagBurst n;
    n.init(5);
    n.onNagSample(3, 100);
    int ampLight = n.lastAmplitude();
    TEST_ASSERT_TRUE(ampLight >= 60 && ampLight <= 75);

    DashReactiveNagBurst n2;
    n2.init(6);
    n2.onNagSample(13, 100);
    int ampHeavy = n2.lastAmplitude();
    TEST_ASSERT_TRUE(ampHeavy >= 80 && ampHeavy <= 95);
}

// 7. applyToFrame：base（从帧字节解码）+ human_weight(8) + pert 写回 data[2:3]
void test_apply_to_frame()
{
    DashReactiveNagBurst n;
    n.init(7);
    uint8_t d2lo = 0x08, d3 = 0x12; // base = 0x0812 = 2066
    n.applyToFrame(d2lo, d3, 50);   // +human_weight 8 + pert 50 = +58 → 0x084C
    int16_t out = (int16_t)(((uint16_t)d2lo << 8) | d3);
    TEST_ASSERT_EQUAL_INT16(0x0812 + 8 + 50, out);
    TEST_ASSERT_EQUAL_UINT8(0x08, d2lo);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_nag_no_inject);
    RUN_TEST(test_nag_starts_burst);
    RUN_TEST(test_burst_gap_enforced);
    RUN_TEST(test_cooldown_after_three_bursts);
    RUN_TEST(test_wave_half_sine_shape);
    RUN_TEST(test_amplitude_tiers_and_cap);
    RUN_TEST(test_apply_to_frame);
    return UNITY_END();
}
