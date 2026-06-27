#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

static void feedFrames(DashReactiveNagBurst &n, int count, unsigned long startMs = 1000)
{
    for (int i = 0; i < count; ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(startMs + i * 40));
        (void)n.nextReplayDelta(startMs + i * 40);
    }
}

void test_inactive_hos_does_not_start_replay()
{
    DashReactiveNagBurst n;
    n.init(12345);
    n.onNagSample(3, 100, false);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, n.nagSamples());
    TEST_ASSERT_EQUAL_UINT32(0, n.replayAttempts());
    TEST_ASSERT_EQUAL_STRING("toggle", n.blockedReason());
}

void test_hos3_active_starts_first_attempt_with_medium_profile()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(12);
    n.onNagSample(3, 100, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::POS_MED, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(1, n.lastProfileDir());
    TEST_ASSERT_EQUAL_INT(145, n.lastPeakRaw());
}

void test_profile_outputs_exact_positive_medium_sequence()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(10);
    n.onNagSample(3, 100, true);

    const int expected[] = {40, 80, 110, 130, 145, 145, 130, 105, 75, 40};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(100 + i * 40));
        TEST_ASSERT_EQUAL_INT(expected[i], n.nextReplayDelta(100 + i * 40));
    }
    TEST_ASSERT_FALSE(n.shouldEcho(100 + 10 * 40));
    TEST_ASSERT_EQUAL(HumanReplayMode::OBSERVING, n.mode());
}

void test_failed_attempt_retries_opposite_direction()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(15);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);

    n.onNagSample(3, 600, true); // first observation sample, still NAG
    TEST_ASSERT_EQUAL(HumanReplayMode::OBSERVING, n.mode());
    n.onNagSample(3, 1100, true); // second observation sample -> attempt 2

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(2, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::NEG_MED, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(-1, n.lastProfileDir());

    const int expected[] = {-40, -80, -110, -130, -145, -145, -130, -105, -75, -40};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(1100 + i * 40));
        TEST_ASSERT_EQUAL_INT(expected[i], n.nextReplayDelta(1100 + i * 40));
    }
}

void test_third_attempt_uses_strong_profile_then_cooldown()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);
    n.onNagSample(3, 600, true);
    n.onNagSample(3, 1100, true);
    feedFrames(n, 10, 1100);
    n.onNagSample(3, 1600, true);
    n.onNagSample(3, 2100, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(3, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::POS_STRONG, n.lastProfileId());
    TEST_ASSERT_EQUAL_INT(175, n.lastPeakRaw());

    feedFrames(n, 10, 2100);
    n.onNagSample(3, 2600, true);
    n.onNagSample(3, 3100, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(3100));
    TEST_ASSERT_TRUE(n.cooldownRemainMs(3100) > 0);
    TEST_ASSERT_EQUAL_STRING("maxAttempts", n.blockedReason());

    n.onNagSample(3, 6101, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(6101));
    TEST_ASSERT_EQUAL_UINT32(4, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::POS_MED, n.lastProfileId());
}

void test_active_false_preserves_attempt_budget_during_continuous_nag()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);

    n.onNagSample(3, 600, false);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(600));
    TEST_ASSERT_EQUAL_STRING("toggle", n.blockedReason());

    n.onNagSample(3, 1100, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(2, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::NEG_MED, n.lastProfileId());
}

void test_cooldown_clear_does_not_count_success()
{
    DashReactiveNagBurst n;
    n.init(4);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    feedFrames(n, 10, 100);
    n.onNagSample(3, 600, true);
    n.onNagSample(3, 1100, true);
    feedFrames(n, 10, 1100);
    n.onNagSample(3, 1600, true);
    n.onNagSample(3, 2100, true);
    feedFrames(n, 10, 2100);
    n.onNagSample(3, 2600, true);
    n.onNagSample(3, 3100, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_UINT32(0, n.replaySuccesses());

    n.onNagSample(2, 3200, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_UINT32(0, n.replaySuccesses());
    TEST_ASSERT_EQUAL_UINT32(1, n.replayFailures());
}

void test_hos_clear_before_emitted_echo_does_not_count_success()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));

    n.onNagSample(2, 180, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(180));
    TEST_ASSERT_EQUAL_UINT32(0, n.replaySuccesses());
}

void test_hos_clear_after_emitted_echo_counts_success_and_stops_replay()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(0);
    n.onNagSample(3, 100, true);
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_INT(40, n.nextReplayDelta(100));
    n.notifyEchoSent();

    n.onNagSample(2, 180, true);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(180));
    TEST_ASSERT_EQUAL_UINT32(1, n.replaySuccesses());
    TEST_ASSERT_EQUAL_UINT8(3, n.lastHosBefore());
    TEST_ASSERT_EQUAL_UINT8(2, n.lastHosAfter());
}

void test_txfail_cooldown_preserves_reason_across_nag_samples()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(3, 100, true);
    n.failReplayTx(140);

    n.onNagSample(3, 200, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());
    TEST_ASSERT_TRUE(n.cooldownRemainMs(200) > 0);
}

void test_hos_clear_during_txfail_cooldown_does_not_bypass_cooldown()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(3, 100, true);
    n.failReplayTx(140);

    n.onNagSample(2, 200, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());
    TEST_ASSERT_TRUE(n.cooldownRemainMs(200) > 0);
}

void test_partial_success_evidence_survives_later_txfail_clear_sample()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(3, 100, true);
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    n.commitReplayDelta(n.peekReplayDelta(100));
    n.notifyEchoSent();
    n.failReplayTx(140);

    n.onNagSample(2, 200, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_UINT32(1, n.replaySuccesses());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());
}

void test_txfail_cooldown_expiry_preserves_attempt_budget()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.onNagSample(3, 100, true);
    n.failReplayTx(140);

    n.onNagSample(3, 3141, true);

    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, n.mode());
    TEST_ASSERT_EQUAL_UINT32(2, n.replayAttempts());
    TEST_ASSERT_EQUAL(DashHumanReplayProfileId::NEG_MED, n.lastProfileId());
}

void test_apply_delta_to_legacy_torque_supports_negative_and_clamps()
{
    DashReactiveNagBurst n;
    n.init(6);

    uint8_t d2lo = 0x08;
    uint8_t d3 = 0x00; // signed 0
    n.applyDeltaToFrame(d2lo, d3, -175);
    int signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(-175, signedOut);

    d2lo = 0x08;
    d3 = 0x00;
    n.applyDeltaToFrame(d2lo, d3, 999);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(180, signedOut);

    d2lo = 0x08;
    d3 = 0x00;
    n.applyDeltaToFrame(d2lo, d3, -999);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(-180, signedOut);

    d2lo = 0x08;
    d3 = 0x80; // signed +128
    n.applyDeltaToFrame(d2lo, d3, 180);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(220, signedOut);

    d2lo = 0x07;
    d3 = 0x80; // signed -128
    n.applyDeltaToFrame(d2lo, d3, -180);
    signedOut = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
    TEST_ASSERT_EQUAL_INT(-220, signedOut);
}

void test_diag_reports_v3_fields()
{
    DashReactiveNagBurst n;
    n.init(2);
    n.noteBaseTorqueRaw(12);
    n.onNagSample(3, 100, true);
    (void)n.nextReplayDelta(100);
    n.notifyEchoSent();

    DashReactiveDiag d = n.diag(140);
    TEST_ASSERT_EQUAL(HumanReplayMode::REPLAYING, d.mode);
    TEST_ASSERT_TRUE(d.injecting);
    TEST_ASSERT_EQUAL_UINT8(3, d.lastHandsOnState);
    TEST_ASSERT_EQUAL_UINT32(1, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(1, d.replayAttempts);
    TEST_ASSERT_EQUAL_UINT32(0, d.replaySuccesses);
    TEST_ASSERT_EQUAL_INT(40, d.lastOutDeltaRaw);
    TEST_ASSERT_EQUAL_INT(145, d.lastPeakRaw);
    TEST_ASSERT_EQUAL_INT(12, d.lastBaseRaw);
    TEST_ASSERT_EQUAL_UINT32(1, d.echoSent);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_inactive_hos_does_not_start_replay);
    RUN_TEST(test_hos3_active_starts_first_attempt_with_medium_profile);
    RUN_TEST(test_profile_outputs_exact_positive_medium_sequence);
    RUN_TEST(test_failed_attempt_retries_opposite_direction);
    RUN_TEST(test_third_attempt_uses_strong_profile_then_cooldown);
    RUN_TEST(test_active_false_preserves_attempt_budget_during_continuous_nag);
    RUN_TEST(test_cooldown_clear_does_not_count_success);
    RUN_TEST(test_hos_clear_before_emitted_echo_does_not_count_success);
    RUN_TEST(test_hos_clear_after_emitted_echo_counts_success_and_stops_replay);
    RUN_TEST(test_txfail_cooldown_preserves_reason_across_nag_samples);
    RUN_TEST(test_hos_clear_during_txfail_cooldown_does_not_bypass_cooldown);
    RUN_TEST(test_partial_success_evidence_survives_later_txfail_clear_sample);
    RUN_TEST(test_txfail_cooldown_expiry_preserves_attempt_budget);
    RUN_TEST(test_apply_delta_to_legacy_torque_supports_negative_and_clamps);
    RUN_TEST(test_diag_reports_v3_fields);
    return UNITY_END();
}
