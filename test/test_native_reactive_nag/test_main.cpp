#include <unity.h>
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

static void startActiveNag(DashReactiveNagBurst &n, unsigned long nowMs = 100)
{
    n.init(12345);
    n.onNagSample(3, nowMs, true, 6);
}

void test_hos_clear_does_not_start_burst()
{
    DashReactiveNagBurst n;
    n.init(12345);

    n.onNagSample(2, 100, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(0, n.nagSamples());
    TEST_ASSERT_EQUAL_UINT32(0, n.burstSessions());
}

void test_inactive_gate_records_toggle_and_stays_idle()
{
    DashReactiveNagBurst n;
    n.init(12345);

    n.onNagSample(3, 100, false, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, n.nagSamples());
    TEST_ASSERT_EQUAL_UINT32(0, n.burstSessions());
    TEST_ASSERT_EQUAL_STRING("toggle", n.blockedReason());
}

void test_hos3_active_enters_burst_on_for_1000ms()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(100));
    TEST_ASSERT_TRUE(n.shouldEcho(1099));
    TEST_ASSERT_EQUAL_UINT32(1, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOnEntries());
    TEST_ASSERT_EQUAL_UINT32(0, n.burstOffEntries());
    TEST_ASSERT_EQUAL_UINT32(1, n.phaseRemainMs(1099));
}

void test_burst_on_transitions_to_burst_off_after_1000ms()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 1100, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(1100));
    TEST_ASSERT_FALSE(n.shouldEcho(2599));
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOffEntries());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstCyclesCompleted());
}

void test_burst_off_returns_to_burst_on_after_1500ms_if_nag_persists()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.onNagSample(3, 1100, true, 6);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, n.mode());

    n.onNagSample(3, 2600, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(2600));
    TEST_ASSERT_EQUAL_UINT32(2, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(2, n.burstOnEntries());
}

void test_tsl6p_torque_sequence_cycles_during_burst_on()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    const int expected[] = {180, 150, -150, -180, 180, 150};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        TEST_ASSERT_TRUE(n.shouldEcho(100 + i * 40));
        int target = n.peekReplayDelta(100 + i * 40);
        TEST_ASSERT_EQUAL_INT(expected[i], target);
        n.commitReplayDelta(target);
        n.notifyEchoSent();
    }

    TEST_ASSERT_EQUAL_UINT32(6, n.echoSent());
    TEST_ASSERT_EQUAL_INT(150, n.lastOutDeltaRaw());
}

void test_burst_off_sends_no_echo_even_with_persistent_nag()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.onNagSample(3, 1100, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(1200));
    TEST_ASSERT_EQUAL_INT(0, n.peekReplayDelta(1200));
}

void test_hos_clear_during_burst_on_counts_on_clear_and_stops()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    TEST_ASSERT_TRUE(n.shouldEcho(140));
    int target = n.peekReplayDelta(140);
    n.commitReplayDelta(target);
    n.notifyEchoSent();

    n.onNagSample(2, 200, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(200));
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearEvents());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearDuringOn());
    TEST_ASSERT_EQUAL_UINT32(0, n.hosClearDuringOff());
    TEST_ASSERT_EQUAL_UINT8(3, n.lastHosBefore());
    TEST_ASSERT_EQUAL_UINT8(2, n.lastHosAfter());
}

void test_hos_clear_during_burst_off_counts_off_clear_and_stops()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.commitReplayDelta(n.peekReplayDelta(140));
    n.notifyEchoSent();
    n.onNagSample(3, 1100, true, 6);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, n.mode());

    n.onNagSample(2, 1500, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearEvents());
    TEST_ASSERT_EQUAL_UINT32(0, n.hosClearDuringOn());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearDuringOff());
}

void test_gate_loss_cancels_burst_with_reason()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 200, false, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(200));
    TEST_ASSERT_EQUAL_STRING("toggle", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(1, n.gateBlocks());
}

void test_abort_state_enters_cooldown_and_blocks_echo()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 200, true, 8);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(200));
    TEST_ASSERT_EQUAL_STRING("abort", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(1, n.abortBlocks());
    TEST_ASSERT_TRUE(n.cooldownRemainMs(200) > 0);
}

void test_abort_cooldown_expires_to_new_burst_if_nag_persists()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.onNagSample(3, 200, true, 9);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());

    n.onNagSample(3, 3201, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(3201));
    TEST_ASSERT_EQUAL_UINT32(2, n.burstSessions());
}


void test_abort_state_with_hos_clear_still_enters_cooldown_and_blocks_restart()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, n.mode());

    n.onNagSample(2, 200, true, 8);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(200));
    TEST_ASSERT_EQUAL_STRING("abort", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(1, n.abortBlocks());

    n.onNagSample(3, 2500, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(2500));
    TEST_ASSERT_EQUAL_STRING("abort", n.blockedReason());
    TEST_ASSERT_TRUE(n.cooldownRemainMs(2500) > 0);
}

void test_delayed_sample_advances_to_scheduled_burst_off_boundary()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 2000, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(2000));
    TEST_ASSERT_EQUAL_UINT32(600, n.phaseRemainMs(2000));
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOffEntries());
}

void test_delayed_sample_advances_through_off_to_next_on_boundary()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 2600, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, n.mode());
    TEST_ASSERT_TRUE(n.shouldEcho(2600));
    TEST_ASSERT_EQUAL_UINT32(1000, n.phaseRemainMs(2600));
    TEST_ASSERT_EQUAL_UINT32(2, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(2, n.burstOnEntries());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOffEntries());
}

void test_txfail_enters_cooldown_and_records_failure()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.failReplayTx(160);

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(1, n.txFailures());
    TEST_ASSERT_EQUAL_UINT32(1, n.replayFailures());
}

void test_set_signed_torque_in_frame_targets_absolute_torque_and_clamps()
{
    uint8_t d2lo = 0x08;
    uint8_t d3 = 0x14; // incoming +20 signed raw

    DashReactiveNagBurst::setSignedTorqueInFrame(d2lo, d3, 180);
    TEST_ASSERT_EQUAL_INT(180, DashReactiveNagBurst::decodeSignedTorque(d2lo, d3));

    DashReactiveNagBurst::setSignedTorqueInFrame(d2lo, d3, 999);
    TEST_ASSERT_EQUAL_INT(180, DashReactiveNagBurst::decodeSignedTorque(d2lo, d3));

    DashReactiveNagBurst::setSignedTorqueInFrame(d2lo, d3, -999);
    TEST_ASSERT_EQUAL_INT(-180, DashReactiveNagBurst::decodeSignedTorque(d2lo, d3));
}

void test_diag_reports_v4_burst_fields()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    int target = n.peekReplayDelta(140);
    n.commitReplayDelta(target);
    n.notifyEchoSent();

    DashReactiveDiag d = n.diag(140);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, d.mode);
    TEST_ASSERT_TRUE(d.injecting);
    TEST_ASSERT_EQUAL_UINT32(1, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(1, d.burstSessions);
    TEST_ASSERT_EQUAL_UINT32(1, d.burstOnEntries);
    TEST_ASSERT_EQUAL_UINT32(0, d.burstOffEntries);
    TEST_ASSERT_EQUAL_UINT32(1, d.burstFramesSent);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearEvents);
    TEST_ASSERT_EQUAL_UINT8(3, d.lastHosBefore);
    TEST_ASSERT_EQUAL_UINT8(6, d.lastApState);
    TEST_ASSERT_EQUAL_INT(180, d.lastTorqueRaw);
    TEST_ASSERT_EQUAL_INT(180, d.lastTorqueNmX100);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_hos_clear_does_not_start_burst);
    RUN_TEST(test_inactive_gate_records_toggle_and_stays_idle);
    RUN_TEST(test_hos3_active_enters_burst_on_for_1000ms);
    RUN_TEST(test_burst_on_transitions_to_burst_off_after_1000ms);
    RUN_TEST(test_burst_off_returns_to_burst_on_after_1500ms_if_nag_persists);
    RUN_TEST(test_tsl6p_torque_sequence_cycles_during_burst_on);
    RUN_TEST(test_burst_off_sends_no_echo_even_with_persistent_nag);
    RUN_TEST(test_hos_clear_during_burst_on_counts_on_clear_and_stops);
    RUN_TEST(test_hos_clear_during_burst_off_counts_off_clear_and_stops);
    RUN_TEST(test_gate_loss_cancels_burst_with_reason);
    RUN_TEST(test_abort_state_enters_cooldown_and_blocks_echo);
    RUN_TEST(test_abort_cooldown_expires_to_new_burst_if_nag_persists);
    RUN_TEST(test_abort_state_with_hos_clear_still_enters_cooldown_and_blocks_restart);
    RUN_TEST(test_delayed_sample_advances_to_scheduled_burst_off_boundary);
    RUN_TEST(test_delayed_sample_advances_through_off_to_next_on_boundary);
    RUN_TEST(test_txfail_enters_cooldown_and_records_failure);
    RUN_TEST(test_set_signed_torque_in_frame_targets_absolute_torque_and_clamps);
    RUN_TEST(test_diag_reports_v4_burst_fields);
    return UNITY_END();
}
