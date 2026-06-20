#include <unity.h>
#include "dash_bionic_steer.h"

// ─── DashBionicPRNG tests ─────────────────────────────────────────────

void test_prng_seed_zero_falls_back_to_deadbeef()
{
    DashBionicPRNG rng;
    rng.seed(0);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, rng.s);
}

void test_prng_seed_nonzero_sets_state()
{
    DashBionicPRNG rng;
    rng.seed(12345);
    TEST_ASSERT_EQUAL_UINT32(12345u, rng.s);
}

void test_prng_next_is_deterministic()
{
    DashBionicPRNG a, b;
    a.seed(42);
    b.seed(42);
    for (int i = 0; i < 20; i++)
        TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
}

void test_prng_next_changes_state()
{
    DashBionicPRNG rng;
    rng.seed(42);
    uint32_t s0 = rng.s;
    rng.next();
    TEST_ASSERT_TRUE(rng.s != s0);
}

void test_prng_range_within_bounds()
{
    DashBionicPRNG rng;
    rng.seed(42);
    for (int i = 0; i < 200; i++)
    {
        uint32_t v = rng.range(30, 55);
        TEST_ASSERT_TRUE(v >= 30);
        TEST_ASSERT_TRUE(v <= 55);
    }
}

// ─── DashBionicSteer constants ────────────────────────────────────────

void test_bionic_constants()
{
    TEST_ASSERT_EQUAL(60, DashBionicSteer::kPerturbCap);
    TEST_ASSERT_EQUAL(30, DashBionicSteer::kAmplitudeLo);
    TEST_ASSERT_EQUAL(55, DashBionicSteer::kAmplitudeHi);
    TEST_ASSERT_EQUAL(350, DashBionicSteer::kDurationLo);
    TEST_ASSERT_EQUAL(500, DashBionicSteer::kDurationHi);
    TEST_ASSERT_EQUAL(3, DashBionicSteer::kMaxConsecutiveFails);
    TEST_ASSERT_EQUAL_HEX16(0x08B6, DashBionicSteer::kBaseTorqueRaw);
}

// ─── beginPhase parameters in range ───────────────────────────────────

void test_begin_phase_amplitude_in_range()
{
    DashBionicSteer bs;
    bs.init(42);
    for (int i = 0; i < 50; i++)
    {
        bs.beginPhase();
        TEST_ASSERT_TRUE(bs.amplitude >= DashBionicSteer::kAmplitudeLo);
        TEST_ASSERT_TRUE(bs.amplitude <= DashBionicSteer::kAmplitudeHi);
    }
}

void test_begin_phase_duration_in_range()
{
    DashBionicSteer bs;
    bs.init(99);
    for (int i = 0; i < 50; i++)
    {
        bs.beginPhase();
        TEST_ASSERT_TRUE(bs.phaseDurationMs >= DashBionicSteer::kDurationLo);
        TEST_ASSERT_TRUE(bs.phaseDurationMs <= DashBionicSteer::kDurationHi);
    }
}

void test_begin_phase_direction_is_plus_or_minus_1()
{
    DashBionicSteer bs;
    bs.init(77);
    for (int i = 0; i < 50; i++)
    {
        bs.beginPhase();
        TEST_ASSERT_TRUE(bs.direction == 1 || bs.direction == -1);
    }
}

void test_begin_phase_resets_elapsed_and_phase()
{
    DashBionicSteer bs;
    bs.init(42);
    bs.beginPhase();
    // advance partially
    bs.computePerturbation();
    bs.computePerturbation();
    TEST_ASSERT_TRUE(bs.phaseElapsedMs > 0);
    TEST_ASSERT_TRUE(bs.phase > 0.0f);

    bs.beginPhase();
    TEST_ASSERT_EQUAL(0, bs.phaseElapsedMs);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bs.phase);
}

// ─── computePerturbation range and cap ────────────────────────────────

void test_perturbation_within_cap()
{
    DashBionicSteer bs;
    bs.init(1234);
    for (int trial = 0; trial < 20; trial++)
    {
        bs.beginPhase();
        while (!bs.needsNewPhase())
        {
            int p = bs.computePerturbation();
            TEST_ASSERT_TRUE(p >= -DashBionicSteer::kPerturbCap);
            TEST_ASSERT_TRUE(p <= DashBionicSteer::kPerturbCap);
        }
    }
}

void test_perturbation_returns_zero_when_phase_exhausted()
{
    DashBionicSteer bs;
    bs.init(42);
    bs.beginPhase();
    // exhaust the phase
    while (!bs.needsNewPhase())
        bs.computePerturbation();
    // now should return 0
    TEST_ASSERT_EQUAL(0, bs.computePerturbation());
}

// ─── needsNewPhase / phase exhaustion ─────────────────────────────────

void test_needs_new_phase_after_elapsed_exceeds_duration()
{
    DashBionicSteer bs;
    bs.init(42);
    bs.beginPhase();
    int duration = bs.phaseDurationMs;
    int elapsed = 0;
    while (elapsed < duration)
    {
        TEST_ASSERT_FALSE(bs.needsNewPhase());
        bs.computePerturbation();
        elapsed += DashBionicSteer::kFramePeriodMs;
    }
    TEST_ASSERT_TRUE(bs.needsNewPhase());
}

// ─── applyToFrame encoding/decoding ───────────────────────────────────

void test_apply_to_frame_roundtrip_zero_perturbation()
{
    // With zero perturbation, torque should be unchanged
    uint8_t data2Lo = (DashBionicSteer::kBaseTorqueRaw >> 8) & 0x0F; // 0x08
    uint8_t data3   = DashBionicSteer::kBaseTorqueRaw & 0xFF;       // 0xB6

    DashBionicSteer bs;
    bs.applyToFrame(data2Lo, data3, 0);

    TEST_ASSERT_EQUAL_HEX8(0x08, data2Lo);
    TEST_ASSERT_EQUAL_HEX8(0xB6, data3);
}

void test_apply_to_frame_positive_perturbation()
{
    uint8_t data2Lo = (DashBionicSteer::kBaseTorqueRaw >> 8) & 0x0F;
    uint8_t data3   = DashBionicSteer::kBaseTorqueRaw & 0xFF;

    DashBionicSteer bs;
    bs.applyToFrame(data2Lo, data3, 50);

    uint16_t torque = (static_cast<uint16_t>(data2Lo) << 8) | data3;
    uint16_t expected = DashBionicSteer::kBaseTorqueRaw + 50;
    TEST_ASSERT_EQUAL_UINT16(expected, torque);
}

void test_apply_to_frame_negative_perturbation()
{
    uint8_t data2Lo = (DashBionicSteer::kBaseTorqueRaw >> 8) & 0x0F;
    uint8_t data3   = DashBionicSteer::kBaseTorqueRaw & 0xFF;

    DashBionicSteer bs;
    bs.applyToFrame(data2Lo, data3, -30);

    uint16_t torque = (static_cast<uint16_t>(data2Lo) << 8) | data3;
    uint16_t expected = DashBionicSteer::kBaseTorqueRaw - 30;
    TEST_ASSERT_EQUAL_UINT16(expected, torque);
}

void test_apply_to_frame_large_positive_clamped_by_caller()
{
    // If perturbation exceeds 12-bit range, verify it wraps (signed arithmetic)
    // In practice, perturbation is capped at 60 by computePerturbation
    uint8_t data2Lo = (DashBionicSteer::kBaseTorqueRaw >> 8) & 0x0F;
    uint8_t data3   = DashBionicSteer::kBaseTorqueRaw & 0xFF;

    DashBionicSteer bs;
    // Apply kPerturbCap — should work fine since base + 60 stays in 12-bit range
    bs.applyToFrame(data2Lo, data3, DashBionicSteer::kPerturbCap);

    uint16_t torque = (static_cast<uint16_t>(data2Lo) << 8) | data3;
    TEST_ASSERT_EQUAL_UINT16(DashBionicSteer::kBaseTorqueRaw + 60, torque);
}

// ─── Safety: failure auto-disable ─────────────────────────────────────

void test_report_failure_disables_after_3()
{
    DashBionicSteer bs;
    TEST_ASSERT_FALSE(bs.isDisabled());

    bs.reportFailure();
    TEST_ASSERT_FALSE(bs.isDisabled());

    bs.reportFailure();
    TEST_ASSERT_FALSE(bs.isDisabled());

    bs.reportFailure(); // 3rd fail → disabled
    TEST_ASSERT_TRUE(bs.isDisabled());
    TEST_ASSERT_EQUAL(3, bs.consecutiveFails);
}

void test_report_success_resets_fail_counter()
{
    DashBionicSteer bs;
    bs.reportFailure();
    bs.reportFailure();
    TEST_ASSERT_EQUAL(2, bs.consecutiveFails);

    bs.reportSuccess();
    TEST_ASSERT_EQUAL(0, bs.consecutiveFails);
    TEST_ASSERT_FALSE(bs.isDisabled());
}

void test_reset_clears_disabled_state()
{
    DashBionicSteer bs;
    for (int i = 0; i < 3; i++) bs.reportFailure();
    TEST_ASSERT_TRUE(bs.isDisabled());

    bs.reset();
    TEST_ASSERT_FALSE(bs.isDisabled());
    TEST_ASSERT_EQUAL(0, bs.consecutiveFails);
}

// ─── init seeds PRNG ──────────────────────────────────────────────────

void test_init_seeds_prng()
{
    DashBionicSteer bs;
    bs.init(12345);
    TEST_ASSERT_EQUAL_UINT32(12345u, bs.rng.s);
}

void test_init_zero_seed_fallback()
{
    DashBionicSteer bs;
    bs.init(0);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, bs.rng.s);
}

// ── main ──────────────────────────────────────────────────────────────

int main()
{
    UNITY_BEGIN();

    // PRNG
    RUN_TEST(test_prng_seed_zero_falls_back_to_deadbeef);
    RUN_TEST(test_prng_seed_nonzero_sets_state);
    RUN_TEST(test_prng_next_is_deterministic);
    RUN_TEST(test_prng_next_changes_state);
    RUN_TEST(test_prng_range_within_bounds);

    // Constants
    RUN_TEST(test_bionic_constants);

    // beginPhase
    RUN_TEST(test_begin_phase_amplitude_in_range);
    RUN_TEST(test_begin_phase_duration_in_range);
    RUN_TEST(test_begin_phase_direction_is_plus_or_minus_1);
    RUN_TEST(test_begin_phase_resets_elapsed_and_phase);

    // computePerturbation
    RUN_TEST(test_perturbation_within_cap);
    RUN_TEST(test_perturbation_returns_zero_when_phase_exhausted);

    // needsNewPhase
    RUN_TEST(test_needs_new_phase_after_elapsed_exceeds_duration);

    // applyToFrame
    RUN_TEST(test_apply_to_frame_roundtrip_zero_perturbation);
    RUN_TEST(test_apply_to_frame_positive_perturbation);
    RUN_TEST(test_apply_to_frame_negative_perturbation);
    RUN_TEST(test_apply_to_frame_large_positive_clamped_by_caller);

    // Safety
    RUN_TEST(test_report_failure_disables_after_3);
    RUN_TEST(test_report_success_resets_fail_counter);
    RUN_TEST(test_reset_clears_disabled_state);

    // Init
    RUN_TEST(test_init_seeds_prng);
    RUN_TEST(test_init_zero_seed_fallback);

    return UNITY_END();
}
