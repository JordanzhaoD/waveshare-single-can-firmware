#include <unity.h>
#include "dash_legacy_smart_offset.h"

static LegacySmartOffsetConfig cfg;
static LegacySmartOffsetEngine engine;

void setUp()
{
    cfg = LegacySmartOffsetConfig{};
    engine = LegacySmartOffsetEngine{};
}

void tearDown() {}

static LegacySmartOffsetResult compute(uint8_t rawLimit, uint32_t nowMs, bool engaged)
{
    return engine.compute(cfg, rawLimit, nowMs, engaged);
}

void test_off_mode_outputs_zero()
{
    cfg.mode = LegacySmartOffsetMode::Off;
    cfg.manualOffsetKph = 12;
    LegacySmartOffsetResult r = compute(12, 1000, true);
    TEST_ASSERT_EQUAL_UINT8(0, r.outputOffsetKph);
    TEST_ASSERT_EQUAL_STRING("off", r.blockedReason);
}

void test_manual_mode_clamps_to_33()
{
    cfg.mode = LegacySmartOffsetMode::Manual;
    cfg.manualOffsetKph = 99;
    LegacySmartOffsetResult r = compute(12, 1000, true);
    TEST_ASSERT_EQUAL_UINT8(33, r.outputOffsetKph);
    TEST_ASSERT_FALSE(r.fallbackUsed);
}

void test_auto_35kph_uses_63pct_and_cap60()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult r = compute(7, 1000, true);
    TEST_ASSERT_EQUAL_UINT16(35, r.speedLimitKph);
    TEST_ASSERT_EQUAL_UINT8(63, r.offsetPct);
    TEST_ASSERT_EQUAL_UINT16(60, r.absoluteCapKph);
    TEST_ASSERT_EQUAL_UINT8(22, r.outputOffsetKph);
}

void test_auto_45kph_uses_cap67()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult r = compute(9, 1000, true);
    TEST_ASSERT_EQUAL_UINT16(45, r.speedLimitKph);
    TEST_ASSERT_EQUAL_UINT16(67, r.absoluteCapKph);
    TEST_ASSERT_EQUAL_UINT8(22, r.outputOffsetKph);
}

void test_auto_60kph_uses_cap90_and_outputs_30()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult r = compute(12, 1000, true);
    TEST_ASSERT_EQUAL_UINT16(60, r.speedLimitKph);
    TEST_ASSERT_EQUAL_UINT8(50, r.offsetPct);
    TEST_ASSERT_EQUAL_UINT16(90, r.absoluteCapKph);
    TEST_ASSERT_EQUAL_UINT8(30, r.outputOffsetKph);
}

void test_auto_80kph_uses_cap104_and_outputs_24()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult r = compute(16, 1000, true);
    TEST_ASSERT_EQUAL_UINT16(80, r.speedLimitKph);
    TEST_ASSERT_EQUAL_UINT8(30, r.offsetPct);
    TEST_ASSERT_EQUAL_UINT16(104, r.absoluteCapKph);
    TEST_ASSERT_EQUAL_UINT8(24, r.outputOffsetKph);
}

void test_auto_120kph_uses_cap132_and_outputs_12()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult r = compute(24, 1000, true);
    TEST_ASSERT_EQUAL_UINT16(120, r.speedLimitKph);
    TEST_ASSERT_EQUAL_UINT8(10, r.offsetPct);
    TEST_ASSERT_EQUAL_UINT16(132, r.absoluteCapKph);
    TEST_ASSERT_EQUAL_UINT8(12, r.outputOffsetKph);
}

void test_custom_mode_selects_bands_and_clamps_pct()
{
    cfg.mode = LegacySmartOffsetMode::Custom;
    cfg.customPctLow = 70;
    cfg.customPctMid = 31;
    cfg.customPctHigh = 21;
    cfg.customPctVeryHigh = 11;

    LegacySmartOffsetResult low = compute(10, 1000, true);
    TEST_ASSERT_EQUAL_UINT8(63, low.offsetPct);

    LegacySmartOffsetResult mid = compute(14, 1040, true);
    TEST_ASSERT_EQUAL_UINT8(31, mid.offsetPct);

    LegacySmartOffsetResult high = compute(20, 1080, true);
    TEST_ASSERT_EQUAL_UINT8(21, high.offsetPct);

    LegacySmartOffsetResult veryHigh = compute(24, 1120, true);
    TEST_ASSERT_EQUAL_UINT8(11, veryHigh.offsetPct);
}

void test_unknown_limit_auto_falls_back_to_manual()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    cfg.manualOffsetKph = 8;
    LegacySmartOffsetResult r = compute(0, 1000, true);
    TEST_ASSERT_EQUAL_UINT8(8, r.outputOffsetKph);
    TEST_ASSERT_TRUE(r.fallbackUsed);
    TEST_ASSERT_EQUAL_STRING("speedLimitUnknown", r.blockedReason);
}

void test_sna_limit_custom_without_fallback_outputs_zero()
{
    cfg.mode = LegacySmartOffsetMode::Custom;
    cfg.manualOffsetKph = 0;
    LegacySmartOffsetResult r = compute(31, 1000, true);
    TEST_ASSERT_EQUAL_UINT8(0, r.outputOffsetKph);
    TEST_ASSERT_TRUE(r.fallbackUsed);
    TEST_ASSERT_EQUAL_STRING("speedLimitUnknown", r.blockedReason);
}

void test_speed_up_follows_immediately()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    LegacySmartOffsetResult low = compute(12, 1000, true);
    LegacySmartOffsetResult high = compute(18, 1500, true);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(high.rawTargetKph, high.smoothedTargetKph);
    TEST_ASSERT_EQUAL_UINT16(high.rawTargetKph, high.smoothedTargetKph);
    TEST_ASSERT_GREATER_THAN_UINT16(low.smoothedTargetKph, high.smoothedTargetKph);
}

void test_speed_down_smooths_when_engaged()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    cfg.smoothDownEnabled = true;
    cfg.smoothDownRateKphS = 5;
    LegacySmartOffsetResult high = compute(20, 1000, true); // 100 -> target 120
    LegacySmartOffsetResult low = compute(8, 2000, true);   // 40 -> raw target 60
    TEST_ASSERT_EQUAL_UINT16(120, high.smoothedTargetKph);
    TEST_ASSERT_EQUAL_UINT16(115, low.smoothedTargetKph);
    TEST_ASSERT_EQUAL_UINT8(33, low.outputOffsetKph);
    TEST_ASSERT_TRUE(low.smoothingActive);
}

void test_speed_down_syncs_when_not_engaged()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    (void)compute(20, 1000, true);
    LegacySmartOffsetResult low = compute(8, 2000, false);
    TEST_ASSERT_EQUAL_UINT16(low.rawTargetKph, low.smoothedTargetKph);
    TEST_ASSERT_FALSE(low.smoothingActive);
}

void test_large_dt_syncs_directly()
{
    cfg.mode = LegacySmartOffsetMode::Auto;
    (void)compute(20, 1000, true);
    LegacySmartOffsetResult low = compute(8, 12000, true);
    TEST_ASSERT_EQUAL_UINT16(low.rawTargetKph, low.smoothedTargetKph);
    TEST_ASSERT_FALSE(low.smoothingActive);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_off_mode_outputs_zero);
    RUN_TEST(test_manual_mode_clamps_to_33);
    RUN_TEST(test_auto_35kph_uses_63pct_and_cap60);
    RUN_TEST(test_auto_45kph_uses_cap67);
    RUN_TEST(test_auto_60kph_uses_cap90_and_outputs_30);
    RUN_TEST(test_auto_80kph_uses_cap104_and_outputs_24);
    RUN_TEST(test_auto_120kph_uses_cap132_and_outputs_12);
    RUN_TEST(test_custom_mode_selects_bands_and_clamps_pct);
    RUN_TEST(test_unknown_limit_auto_falls_back_to_manual);
    RUN_TEST(test_sna_limit_custom_without_fallback_outputs_zero);
    RUN_TEST(test_speed_up_follows_immediately);
    RUN_TEST(test_speed_down_smooths_when_engaged);
    RUN_TEST(test_speed_down_syncs_when_not_engaged);
    RUN_TEST(test_large_dt_syncs_directly);
    return UNITY_END();
}
