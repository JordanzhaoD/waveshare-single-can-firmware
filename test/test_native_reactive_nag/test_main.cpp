#include <unity.h>
#include "dash_nag_mode.h"
#include "dash_reactive_hold_nag.h"
#include "dash_reactive_nag.h"

void setUp() {}
void tearDown() {}

static void startActiveNag(DashReactiveNagBurst &n, unsigned long nowMs = 100)
{
    n.init(12345);
    n.onNagSample(3, nowMs, true, 6);
}

void test_dash_nag_mode_numeric_mapping_and_names()
{
    TEST_ASSERT_EQUAL_UINT8(0, dashNagModeToRaw(DashNagMode::Off));
    TEST_ASSERT_EQUAL_UINT8(1, dashNagModeToRaw(DashNagMode::HumanReplayTsl6p));
    TEST_ASSERT_EQUAL_UINT8(2, dashNagModeToRaw(DashNagMode::EpasLateEcho));
    TEST_ASSERT_EQUAL_UINT8(3, dashNagModeToRaw(DashNagMode::ReactiveHold));

    TEST_ASSERT_TRUE(dashNagModeIsValid(0));
    TEST_ASSERT_TRUE(dashNagModeIsValid(3));
    TEST_ASSERT_FALSE(dashNagModeIsValid(4));
    TEST_ASSERT_FALSE(dashNagModeIsValid(255));

    TEST_ASSERT_EQUAL(DashNagMode::Off, dashNagModeFromRaw(0));
    TEST_ASSERT_EQUAL(DashNagMode::HumanReplayTsl6p, dashNagModeFromRaw(1));
    TEST_ASSERT_EQUAL(DashNagMode::EpasLateEcho, dashNagModeFromRaw(2));
    TEST_ASSERT_EQUAL(DashNagMode::ReactiveHold, dashNagModeFromRaw(3));
    TEST_ASSERT_EQUAL(DashNagMode::Off, dashNagModeFromRaw(255));

    TEST_ASSERT_EQUAL_STRING("off", dashNagModeName(DashNagMode::Off));
    TEST_ASSERT_EQUAL_STRING("human_replay_tsl6p", dashNagModeName(DashNagMode::HumanReplayTsl6p));
    TEST_ASSERT_EQUAL_STRING("late_echo", dashNagModeName(DashNagMode::EpasLateEcho));
    TEST_ASSERT_EQUAL_STRING("reactive_hold", dashNagModeName(DashNagMode::ReactiveHold));
    TEST_ASSERT_EQUAL_STRING("off", dashNagModeName(static_cast<DashNagMode>(255)));
}

void test_dash_nag_mode_parser_accepts_only_exact_numeric_modes()
{
    DashNagMode parsed = DashNagMode::ReactiveHold;

    TEST_ASSERT_TRUE(dashTryParseNagMode("0", parsed));
    TEST_ASSERT_EQUAL(DashNagMode::Off, parsed);
    TEST_ASSERT_TRUE(dashTryParseNagMode("1", parsed));
    TEST_ASSERT_EQUAL(DashNagMode::HumanReplayTsl6p, parsed);
    TEST_ASSERT_TRUE(dashTryParseNagMode("2", parsed));
    TEST_ASSERT_EQUAL(DashNagMode::EpasLateEcho, parsed);
    TEST_ASSERT_TRUE(dashTryParseNagMode("3", parsed));
    TEST_ASSERT_EQUAL(DashNagMode::ReactiveHold, parsed);

    const char *invalid[] = {nullptr, "", "+1", "-1", "03", "3x", "4", "99", " 3", "3 ", "\t3", "3\n"};
    for (const char *raw : invalid)
    {
        parsed = DashNagMode::EpasLateEcho;
        TEST_ASSERT_FALSE(dashTryParseNagMode(raw, parsed));
        TEST_ASSERT_EQUAL(DashNagMode::EpasLateEcho, parsed);
    }
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

void test_idle_hos_clear_is_diagnosed_separately_from_on_off_clear()
{
    DashReactiveNagBurst n;
    n.init(12345);

    n.onNagSample(2, 100, true, 6);

    DashReactiveDiag d = n.diag(100);
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, d.mode);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearEvents);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearDuringOn);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearDuringOff);
    TEST_ASSERT_EQUAL_UINT32(1, d.hosClearWhileIdle);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearWhileCooldown);
}

void test_active_cooldown_hos_clear_is_diagnosed_separately_from_on_off_clear()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.failReplayTx(200);

    n.onNagSample(2, 500, true, 6);

    DashReactiveDiag d = n.diag(500);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, d.mode);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearEvents);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearDuringOn);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearDuringOff);
    TEST_ASSERT_EQUAL_UINT32(0, d.hosClearWhileIdle);
    TEST_ASSERT_EQUAL_UINT32(1, d.hosClearWhileCooldown);
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

void test_delayed_hos_clear_advances_phase_before_classifying_clear()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(2, 1500, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(1500));
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearEvents());
    TEST_ASSERT_EQUAL_UINT32(0, n.hosClearDuringOn());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearDuringOff());
}

void test_delayed_hos_clear_at_next_on_boundary_counts_prior_off_clear()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(2, 2600, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(2600));
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearEvents());
    TEST_ASSERT_EQUAL_UINT32(0, n.hosClearDuringOn());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearDuringOff());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOnEntries());
}

void test_delayed_hos_clear_after_next_on_boundary_counts_prior_off_clear()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(2, 2601, true, 6);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(2601));
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearEvents());
    TEST_ASSERT_EQUAL_UINT32(0, n.hosClearDuringOn());
    TEST_ASSERT_EQUAL_UINT32(1, n.hosClearDuringOff());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstOnEntries());
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

void test_repeated_gate_loss_after_cancel_counts_once_and_does_not_restart()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);

    n.onNagSample(3, 200, false, 6, "checkAD");
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_UINT32(1, n.gateBlocks());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstSessions());

    n.advance(240, false, "checkAD");
    n.advance(280, false, "checkAD");
    n.onNagSample(3, 320, false, 6, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_FALSE(n.shouldEcho(320));
    TEST_ASSERT_EQUAL_UINT32(1, n.gateBlocks());
    TEST_ASSERT_EQUAL_UINT32(1, n.burstSessions());
    TEST_ASSERT_EQUAL_UINT32(0, n.echoSent());
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

void test_abort_cooldown_survives_later_checkad_gate_loss()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.onNagSample(3, 200, true, 8);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("abort", n.blockedReason());

    n.onNagSample(3, 500, false, 6, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("abort", n.blockedReason());
    TEST_ASSERT_FALSE(n.shouldEcho(500));
    TEST_ASSERT_TRUE(n.cooldownRemainMs(500) > 0);
}

void test_tx_fail_cooldown_survives_later_checkad_gate_loss()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.failReplayTx(200);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());

    n.onNagSample(3, 500, false, 6, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());
    TEST_ASSERT_EQUAL_STRING("txFail", n.blockedReason());
    TEST_ASSERT_FALSE(n.shouldEcho(500));
    TEST_ASSERT_TRUE(n.cooldownRemainMs(500) > 0);
}

void test_expired_tx_fail_cooldown_then_inactive_gate_reports_current_reason()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.failReplayTx(200);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());

    n.onNagSample(3, 3201, false, 6, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_STRING("checkAD", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(0, n.cooldownRemainMs(3201));
    TEST_ASSERT_FALSE(n.shouldEcho(3201));
}

void test_expired_abort_cooldown_then_inactive_gate_reports_current_reason()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.onNagSample(3, 200, true, 8);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());

    n.onNagSample(3, 3201, false, 6, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_STRING("checkAD", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(0, n.cooldownRemainMs(3201));
    TEST_ASSERT_FALSE(n.shouldEcho(3201));
}

void test_expired_cooldown_then_inactive_370_advance_reports_current_reason()
{
    DashReactiveNagBurst n;
    startActiveNag(n, 100);
    n.failReplayTx(200);
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, n.mode());

    n.advance(3201, false, "checkAD");

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, n.mode());
    TEST_ASSERT_EQUAL_STRING("checkAD", n.blockedReason());
    TEST_ASSERT_EQUAL_UINT32(0, n.cooldownRemainMs(3201));
    TEST_ASSERT_FALSE(n.shouldEcho(3201));
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

static uint32_t drainReactiveHold(DashReactiveHoldNag &engine, uint32_t startMs)
{
    uint32_t nowMs = startMs;
    for (int i = 0; i < 8; ++i)
    {
        const int hold = engine.computeHold(nowMs);
        if (hold > 0)
            TEST_ASSERT_LESS_OR_EQUAL_INT(95, hold);
        if (!engine.shouldEcho(nowMs))
            break;
        nowMs += 500;
    }
    TEST_ASSERT_FALSE(engine.shouldEcho(nowMs));
    return nowMs;
}

void test_reactive_hold_inactive_nag_sample_counts_without_injecting()
{
    DashReactiveHoldNag engine;
    engine.init(12345);

    engine.onNagSample(3, 1000, false);

    DashReactiveHoldDiag d = engine.diag(1000);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Reactive, d.phase);
    TEST_ASSERT_FALSE(d.injecting);
    TEST_ASSERT_EQUAL_UINT8(3, d.lastHandsOnState);
    TEST_ASSERT_EQUAL_UINT32(1, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(0, d.reactiveBursts);
}

void test_reactive_hold_active_hos3_starts_positive_reactive_hold()
{
    DashReactiveHoldNag engine;
    engine.init(12345);

    engine.onNagSample(3, 2000, true);

    DashReactiveHoldDiag d = engine.diag(2000);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Reactive, d.phase);
    TEST_ASSERT_TRUE(d.injecting);
    TEST_ASSERT_EQUAL_UINT32(1, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(1, d.reactiveBursts);
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(2000));
}

void test_reactive_hold_outputs_are_positive_accumulating_and_capped()
{
    DashReactiveHoldNag engine;
    engine.init(2);
    engine.onNagSample(3, 100, true);

    int sum = 0;
    int samples = 0;
    int maximum = 0;
    uint32_t nowMs = 100;
    for (int i = 0; i < 20 && engine.diag(nowMs).injecting; ++i)
    {
        const int hold = engine.computeHold(nowMs);
        TEST_ASSERT_GREATER_THAN_INT(0, hold);
        TEST_ASSERT_LESS_OR_EQUAL_INT(95, hold);
        sum += hold;
        maximum = std::max(maximum, hold);
        samples++;
        nowMs += 100;
    }

    TEST_ASSERT_GREATER_THAN_INT(0, samples);
    TEST_ASSERT_GREATER_THAN_INT(0, sum);
    TEST_ASSERT_LESS_OR_EQUAL_INT(95, maximum);
    TEST_ASSERT_FALSE(engine.diag(nowMs).injecting);
}

void test_reactive_hold_enforces_full_800ms_reactive_cooldown()
{
    DashReactiveHoldNag engine;
    engine.init(5); // deterministic 3 strokes * 393ms
    engine.onNagSample(3, 100, true);
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(493));
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(886));
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(1279));
    TEST_ASSERT_FALSE(engine.shouldEcho(1279));

    engine.onNagSample(3, 2078, true);
    TEST_ASSERT_FALSE(engine.diag(2078).injecting);
    TEST_ASSERT_EQUAL_UINT32(1, engine.diag(2078).reactiveBursts);

    engine.onNagSample(3, 2079, true);
    TEST_ASSERT_TRUE(engine.diag(2079).injecting);
    TEST_ASSERT_EQUAL_UINT32(2, engine.diag(2079).reactiveBursts);
}

void test_reactive_hold_each_hos_zero_through_two_can_start_proactive_hold()
{
    for (uint8_t hos = 0; hos <= 2; ++hos)
    {
        DashReactiveHoldNag engine;
        engine.init(100 + hos);
        engine.onNagSample(hos, 5000, true);

        DashReactiveHoldDiag d = engine.diag(5000);
        TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Proactive, d.phase);
        TEST_ASSERT_TRUE(d.injecting);
        TEST_ASSERT_EQUAL_UINT8(hos, d.lastHandsOnState);
        TEST_ASSERT_EQUAL_UINT32(1, d.proactiveWiggles);
        TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(5000));
    }
}

void test_reactive_hold_hos3_interrupts_proactive_and_enters_reactive()
{
    DashReactiveHoldNag engine;
    engine.init(6);
    engine.onNagSample(1, 5000, true);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Proactive, engine.diag(5000).phase);
    TEST_ASSERT_TRUE(engine.diag(5000).injecting);

    engine.onNagSample(3, 5020, true);

    DashReactiveHoldDiag d = engine.diag(5020);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Reactive, d.phase);
    TEST_ASSERT_TRUE(d.injecting);
    TEST_ASSERT_EQUAL_UINT32(1, d.proactiveWiggles);
    TEST_ASSERT_EQUAL_UINT32(1, d.reactiveBursts);
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(5020));
}

void test_reactive_hold_reset_clears_transient_state_and_preserves_counters()
{
    DashReactiveHoldNag engine;
    engine.init(7);
    engine.setCounters(11, 22, 33, 44);
    engine.onNagSample(3, 1000, true);
    engine.notifyEchoSent();

    engine.reset();

    DashReactiveHoldDiag d = engine.diag(1000);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Idle, d.phase);
    TEST_ASSERT_FALSE(d.injecting);
    TEST_ASSERT_EQUAL_UINT8(0, d.lastHandsOnState);
    TEST_ASSERT_EQUAL_INT(0, d.currentAmp);
    TEST_ASSERT_EQUAL_UINT32(12, d.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(23, d.reactiveBursts);
    TEST_ASSERT_EQUAL_UINT32(33, d.proactiveWiggles);
    TEST_ASSERT_EQUAL_UINT32(45, d.echoSent);
    TEST_ASSERT_EQUAL_UINT32(0, d.nextProactiveInMs);
}

void test_reactive_hold_reset_counters_preserves_active_state_and_seeded_schedule()
{
    DashReactiveHoldNag engine;
    DashReactiveHoldNag control;
    engine.init(77);
    control.init(77);
    engine.onNagSample(1, 100, true);
    control.onNagSample(1, 100, true);

    const int ampBefore = engine.diag(100).currentAmp;
    engine.resetCounters();

    DashReactiveHoldDiag afterReset = engine.diag(100);
    TEST_ASSERT_EQUAL(DashReactiveHoldPhase::Proactive, afterReset.phase);
    TEST_ASSERT_TRUE(afterReset.injecting);
    TEST_ASSERT_EQUAL_INT(ampBefore, afterReset.currentAmp);
    TEST_ASSERT_EQUAL_UINT32(0, afterReset.nagSamples);
    TEST_ASSERT_EQUAL_UINT32(0, afterReset.reactiveBursts);
    TEST_ASSERT_EQUAL_UINT32(0, afterReset.proactiveWiggles);
    TEST_ASSERT_EQUAL_UINT32(0, afterReset.echoSent);

    const uint32_t engineEnd = drainReactiveHold(engine, 100);
    const uint32_t controlEnd = drainReactiveHold(control, 100);
    TEST_ASSERT_EQUAL_UINT32(controlEnd, engineEnd);
    TEST_ASSERT_EQUAL_UINT32(control.diag(controlEnd).nextProactiveInMs,
                             engine.diag(engineEnd).nextProactiveInMs);
}

void test_reactive_hold_init_with_same_seed_is_deterministic()
{
    DashReactiveHoldNag first;
    DashReactiveHoldNag second;
    first.init(0x12345678u);
    second.init(0x12345678u);
    first.onNagSample(1, 100, true);
    second.onNagSample(1, 100, true);

    uint32_t nowMs = 100;
    for (int i = 0; i < 8 && first.diag(nowMs).injecting; ++i)
    {
        TEST_ASSERT_EQUAL(first.diag(nowMs).injecting, second.diag(nowMs).injecting);
        TEST_ASSERT_EQUAL_INT(first.computeHold(nowMs), second.computeHold(nowMs));
        nowMs += 500;
    }

    TEST_ASSERT_EQUAL(first.diag(nowMs).injecting, second.diag(nowMs).injecting);
    TEST_ASSERT_EQUAL_UINT32(first.diag(nowMs).nextProactiveInMs,
                             second.diag(nowMs).nextProactiveInMs);
}

void test_reactive_hold_apply_to_frame_adds_human_weight_and_hold()
{
    DashReactiveHoldNag engine;
    engine.init(8);
    uint8_t data2LowNibble = 0x08;
    uint8_t data3 = 0x12;

    engine.applyToFrame(data2LowNibble, data3, 70);

    const int output = (static_cast<int>(data2LowNibble) << 8) | data3;
    TEST_ASSERT_EQUAL_INT(0x0812 + 8 + 70, output);
}

void test_reactive_hold_uint32_wrap_does_not_extend_burst_indefinitely()
{
    DashReactiveHoldNag engine;
    engine.init(9);
    const uint32_t startMs = UINT32_MAX - 100u;
    engine.onNagSample(3, startMs, true);
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(startMs));

    const uint32_t wrappedTimes[] = {350u, 850u, 1350u, 1850u};
    for (uint32_t nowMs : wrappedTimes)
    {
        if (!engine.diag(nowMs).injecting)
            break;
        TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(nowMs));
    }

    TEST_ASSERT_FALSE(engine.diag(1850u).injecting);
}

void test_reactive_hold_proactive_deadline_is_wrap_safe()
{
    DashReactiveHoldNag engine;
    engine.init(10);
    const uint32_t startMs = UINT32_MAX - 2000u;
    engine.onNagSample(2, startMs, true);
    const uint32_t endedAtMs = drainReactiveHold(engine, startMs);
    const uint32_t remainingMs = engine.diag(endedAtMs).nextProactiveInMs;
    TEST_ASSERT_GREATER_THAN_UINT32(0, remainingMs);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(5000, remainingMs);

    const uint32_t dueAtMs = endedAtMs + remainingMs;
    TEST_ASSERT_TRUE(dueAtMs < endedAtMs);

    engine.onNagSample(2, dueAtMs - 1u, true);
    TEST_ASSERT_FALSE(engine.diag(dueAtMs - 1u).injecting);
    engine.onNagSample(2, dueAtMs, true);
    TEST_ASSERT_TRUE(engine.diag(dueAtMs).injecting);
    TEST_ASSERT_EQUAL_UINT32(2, engine.diag(dueAtMs).proactiveWiggles);
}

void test_reactive_hold_sparse_delay_retires_burst_at_real_time_end()
{
    DashReactiveHoldNag engine;
    engine.init(11);
    engine.onNagSample(3, 100, true);
    TEST_ASSERT_TRUE(engine.shouldEcho(100));

    // Every burst is at most 3 * 400ms. A sample 2000ms later is expired
    // even if computeHold was not called at each intermediate stroke boundary.
    TEST_ASSERT_FALSE(engine.shouldEcho(2100));
    TEST_ASSERT_FALSE(engine.diag(2100).injecting);

    engine.onNagSample(3, 2100, true);
    TEST_ASSERT_TRUE(engine.shouldEcho(2100));
    TEST_ASSERT_EQUAL_UINT32(2, engine.diag(2100).reactiveBursts);
}

void test_reactive_hold_sparse_compute_crosses_strokes_and_emits_terminal_positive()
{
    DashReactiveHoldNag engine;
    engine.init(5); // deterministic 3 strokes * 393ms
    engine.onNagSample(3, 100, true);

    // One delayed call crosses two stroke boundaries without stretching them.
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(1000));
    TEST_ASSERT_TRUE(engine.shouldEcho(1000));

    // At the exact real-time end, the final frame is still positive and the
    // burst is retired only after that terminal value is consumed.
    TEST_ASSERT_GREATER_THAN_INT(0, engine.computeHold(1279));
    TEST_ASSERT_FALSE(engine.shouldEcho(1279));
}

void test_reactive_hold_inactive_sample_does_not_restart_reactive_burst()
{
    DashReactiveHoldNag engine;
    engine.init(12);
    engine.onNagSample(3, 100, true);
    TEST_ASSERT_EQUAL_UINT32(1, engine.diag(100).reactiveBursts);

    // Task 6 gates sends with active separately. The pure engine must retain
    // the in-progress burst instead of clearing its end/cooldown history.
    engine.onNagSample(3, 200, false);
    TEST_ASSERT_TRUE(engine.shouldEcho(200));

    engine.onNagSample(3, 201, true);
    TEST_ASSERT_TRUE(engine.shouldEcho(201));
    TEST_ASSERT_EQUAL_UINT32(1, engine.diag(201).reactiveBursts);
}

void test_reactive_hold_inactive_sample_does_not_restart_proactive_burst()
{
    DashReactiveHoldNag engine;
    engine.init(13);
    engine.onNagSample(1, 100, true);
    TEST_ASSERT_TRUE(engine.shouldEcho(100));
    TEST_ASSERT_EQUAL_UINT32(1, engine.diag(100).proactiveWiggles);

    // Sending is gated by active in Task 6, but the pure engine's burst clock
    // continues. Re-enabling one millisecond later must not bypass the 2-5s
    // interval by creating a second proactive wiggle.
    engine.onNagSample(1, 200, false);
    TEST_ASSERT_TRUE(engine.shouldEcho(200));

    engine.onNagSample(1, 201, true);
    TEST_ASSERT_TRUE(engine.shouldEcho(201));
    TEST_ASSERT_EQUAL_UINT32(1, engine.diag(201).proactiveWiggles);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dash_nag_mode_numeric_mapping_and_names);
    RUN_TEST(test_dash_nag_mode_parser_accepts_only_exact_numeric_modes);
    RUN_TEST(test_hos_clear_does_not_start_burst);
    RUN_TEST(test_inactive_gate_records_toggle_and_stays_idle);
    RUN_TEST(test_idle_hos_clear_is_diagnosed_separately_from_on_off_clear);
    RUN_TEST(test_active_cooldown_hos_clear_is_diagnosed_separately_from_on_off_clear);
    RUN_TEST(test_hos3_active_enters_burst_on_for_1000ms);
    RUN_TEST(test_burst_on_transitions_to_burst_off_after_1000ms);
    RUN_TEST(test_burst_off_returns_to_burst_on_after_1500ms_if_nag_persists);
    RUN_TEST(test_tsl6p_torque_sequence_cycles_during_burst_on);
    RUN_TEST(test_burst_off_sends_no_echo_even_with_persistent_nag);
    RUN_TEST(test_hos_clear_during_burst_on_counts_on_clear_and_stops);
    RUN_TEST(test_hos_clear_during_burst_off_counts_off_clear_and_stops);
    RUN_TEST(test_delayed_hos_clear_advances_phase_before_classifying_clear);
    RUN_TEST(test_delayed_hos_clear_at_next_on_boundary_counts_prior_off_clear);
    RUN_TEST(test_delayed_hos_clear_after_next_on_boundary_counts_prior_off_clear);
    RUN_TEST(test_gate_loss_cancels_burst_with_reason);
    RUN_TEST(test_repeated_gate_loss_after_cancel_counts_once_and_does_not_restart);
    RUN_TEST(test_abort_state_enters_cooldown_and_blocks_echo);
    RUN_TEST(test_abort_cooldown_expires_to_new_burst_if_nag_persists);
    RUN_TEST(test_abort_cooldown_survives_later_checkad_gate_loss);
    RUN_TEST(test_tx_fail_cooldown_survives_later_checkad_gate_loss);
    RUN_TEST(test_expired_tx_fail_cooldown_then_inactive_gate_reports_current_reason);
    RUN_TEST(test_expired_abort_cooldown_then_inactive_gate_reports_current_reason);
    RUN_TEST(test_expired_cooldown_then_inactive_370_advance_reports_current_reason);
    RUN_TEST(test_abort_state_with_hos_clear_still_enters_cooldown_and_blocks_restart);
    RUN_TEST(test_delayed_sample_advances_to_scheduled_burst_off_boundary);
    RUN_TEST(test_delayed_sample_advances_through_off_to_next_on_boundary);
    RUN_TEST(test_txfail_enters_cooldown_and_records_failure);
    RUN_TEST(test_set_signed_torque_in_frame_targets_absolute_torque_and_clamps);
    RUN_TEST(test_diag_reports_v4_burst_fields);
    RUN_TEST(test_reactive_hold_inactive_nag_sample_counts_without_injecting);
    RUN_TEST(test_reactive_hold_active_hos3_starts_positive_reactive_hold);
    RUN_TEST(test_reactive_hold_outputs_are_positive_accumulating_and_capped);
    RUN_TEST(test_reactive_hold_enforces_full_800ms_reactive_cooldown);
    RUN_TEST(test_reactive_hold_each_hos_zero_through_two_can_start_proactive_hold);
    RUN_TEST(test_reactive_hold_hos3_interrupts_proactive_and_enters_reactive);
    RUN_TEST(test_reactive_hold_reset_clears_transient_state_and_preserves_counters);
    RUN_TEST(test_reactive_hold_reset_counters_preserves_active_state_and_seeded_schedule);
    RUN_TEST(test_reactive_hold_init_with_same_seed_is_deterministic);
    RUN_TEST(test_reactive_hold_apply_to_frame_adds_human_weight_and_hold);
    RUN_TEST(test_reactive_hold_uint32_wrap_does_not_extend_burst_indefinitely);
    RUN_TEST(test_reactive_hold_proactive_deadline_is_wrap_safe);
    RUN_TEST(test_reactive_hold_sparse_delay_retires_burst_at_real_time_end);
    RUN_TEST(test_reactive_hold_sparse_compute_crosses_strokes_and_emits_terminal_positive);
    RUN_TEST(test_reactive_hold_inactive_sample_does_not_restart_reactive_burst);
    RUN_TEST(test_reactive_hold_inactive_sample_does_not_restart_proactive_burst);
    return UNITY_END();
}
