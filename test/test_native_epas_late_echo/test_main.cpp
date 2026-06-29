#include <climits>
#include <unity.h>
#include "dash_epas_late_echo.h"

static CanFrame makeEpasFrame(uint8_t counter, uint8_t byte4 = 0x20, int signedTorque = 0)
{
    CanFrame f = {.id = 880, .dlc = 8};
    f.data[0] = 0x12;
    f.data[1] = 0x00;
    uint16_t encoded = static_cast<uint16_t>((signedTorque + 0x800) & 0x0FFF);
    f.data[2] = static_cast<uint8_t>(0x80 | ((encoded >> 8) & 0x0F));
    f.data[3] = static_cast<uint8_t>(encoded & 0xFF);
    f.data[4] = byte4;
    f.data[5] = 0xFE;
    f.data[6] = static_cast<uint8_t>(0x20 | (counter & 0x0F));
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return f;
}

static bool checksumValid370(const CanFrame &f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += f.data[i];
    return static_cast<uint8_t>((sum + 0x73) & 0xFF) == f.data[7];
}

static void refreshChecksum370(CanFrame &f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static int decodeSignedTorque(const CanFrame &f)
{
    return static_cast<int>(((f.data[2] & 0x0F) << 8) | f.data[3]) - 0x800;
}

void setUp() {}
void tearDown() {}

void test_cadence_tracker_becomes_stable_after_eight_40ms_frames()
{
    DashEpasCadenceTracker t;
    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(i), 1000 + i * 40);

    TEST_ASSERT_TRUE(t.stable());
    TEST_ASSERT_TRUE(t.lateEchoEligible(1000 + 7 * 40));
    TEST_ASSERT_EQUAL_UINT8(1, t.counterStep());
    TEST_ASSERT_EQUAL_UINT8(8, t.expectedNextCounter());
    TEST_ASSERT_EQUAL_UINT16(40, t.periodMs());
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(1, t.jitterMs());
    TEST_ASSERT_EQUAL_UINT32(1000 + 8 * 40, t.predictedNextRxMs());
}

void test_cadence_tracker_handles_counter_wrap_progression()
{
    DashEpasCadenceTracker t;
    const uint8_t counters[] = {12, 13, 14, 15, 0, 1, 2, 3};
    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(counters[i]), 1000 + i * 40);

    TEST_ASSERT_TRUE(t.stable());
    TEST_ASSERT_TRUE(t.lateEchoEligible(1000 + 7 * 40));
    TEST_ASSERT_EQUAL_UINT8(1, t.counterStep());
    TEST_ASSERT_EQUAL_UINT8(4, t.expectedNextCounter());
    TEST_ASSERT_EQUAL_UINT16(40, t.periodMs());
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(1, t.jitterMs());
    TEST_ASSERT_EQUAL_UINT32(1000 + 8 * 40, t.predictedNextRxMs());
}

void test_cadence_tracker_blocks_on_jitter_above_threshold()
{
    DashEpasCadenceTracker t;
    const unsigned long times[] = {1000, 1040, 1080, 1120, 1160, 1200, 1248, 1280};
    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(i), times[i]);

    TEST_ASSERT_FALSE(t.stable());
    TEST_ASSERT_FALSE(t.lateEchoEligible(1280));
    TEST_ASSERT_EQUAL_STRING("cadenceUnstable", t.blockedReason());
}

void test_cadence_tracker_blocks_counter_jump()
{
    DashEpasCadenceTracker t;
    const uint8_t counters[] = {0, 1, 2, 3, 4, 5, 9, 10};
    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(counters[i]), 1000 + i * 40);

    TEST_ASSERT_FALSE(t.stable());
    TEST_ASSERT_FALSE(t.lateEchoEligible(1280));
    TEST_ASSERT_EQUAL_STRING("counterUnstable", t.blockedReason());
}

void test_encoder_preserves_source_fields_and_recomputes_checksum()
{
    CanFrame source = makeEpasFrame(7, 0xA5, -12);
    source.data[0] = 0x34;
    source.data[1] = 0xAB;
    source.data[2] = static_cast<uint8_t>((source.data[2] & 0x0F) | 0xA0);
    source.data[5] = 0x7E;
    source.data[6] = static_cast<uint8_t>((source.data[6] & 0x0F) | 0x50);
    refreshChecksum370(source);
    CanFrame out = {};

    TEST_ASSERT_TRUE(DashEpasFaithfulEncoder::build(source, 8, 170, out));

    TEST_ASSERT_EQUAL_UINT32(880, out.id);
    TEST_ASSERT_EQUAL_UINT8(8, out.dlc);
    TEST_ASSERT_EQUAL_HEX8(source.data[0], out.data[0]);
    TEST_ASSERT_EQUAL_HEX8(source.data[1], out.data[1]);
    TEST_ASSERT_EQUAL_HEX8(source.data[2] & 0xF0, out.data[2] & 0xF0);
    TEST_ASSERT_EQUAL_HEX8(source.data[4], out.data[4]);
    TEST_ASSERT_EQUAL_HEX8(source.data[5], out.data[5]);
    TEST_ASSERT_EQUAL_HEX8(source.data[6] & 0xF0, out.data[6] & 0xF0);
    TEST_ASSERT_EQUAL_UINT8(8, out.data[6] & 0x0F);
    TEST_ASSERT_EQUAL_INT(170, decodeSignedTorque(out));
    TEST_ASSERT_TRUE(checksumValid370(out));
}

void test_encoder_clamps_torque_to_180_raw()
{
    CanFrame out = {};
    TEST_ASSERT_TRUE(DashEpasFaithfulEncoder::build(makeEpasFrame(1), 2, 250, out));
    TEST_ASSERT_EQUAL_INT(180, decodeSignedTorque(out));

    TEST_ASSERT_TRUE(DashEpasFaithfulEncoder::build(makeEpasFrame(1), 2, -250, out));
    TEST_ASSERT_EQUAL_INT(-180, decodeSignedTorque(out));
}

void test_late_echo_schedules_at_period_minus_lead()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag d = n.diag(1280);
    TEST_ASSERT_TRUE(d.pendingEcho);
    TEST_ASSERT_EQUAL_UINT32(1000 + 8 * 40 - DashEpasLateEcho::kLateEchoLeadMs, d.pendingSendAtMs);
    TEST_ASSERT_FALSE(n.due(d.pendingSendAtMs - 1));
    TEST_ASSERT_TRUE(n.due(d.pendingSendAtMs));
    TEST_ASSERT_TRUE(n.due(d.pendingSendAtMs + DashEpasLateEcho::kMaxLatenessMs));
    TEST_ASSERT_FALSE(n.due(d.pendingSendAtMs + DashEpasLateEcho::kMaxLatenessMs + 1));
}

void test_new_rx_before_due_cancels_pending_and_counts_missed_window()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    TEST_ASSERT_TRUE(before.pendingEcho);

    n.onEpasFrame(makeEpasFrame(8), before.pendingSendAtMs - 1, true);

    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_UINT32(1, after.lateWindowMissed);
    TEST_ASSERT_EQUAL_STRING("lateWindowMissed", after.blockedReason);
}

void test_due_frame_builds_only_in_late_window_and_preserves_byte4()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i, 0x20), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    CanFrame out = {};
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs - 1, out, true, 3, nullptr));
    TEST_ASSERT_TRUE(n.buildDueFrame(before.pendingSendAtMs, out, true, 3, nullptr));
    TEST_ASSERT_EQUAL_HEX8(0x20, out.data[4]);
    TEST_ASSERT_EQUAL_UINT8(8, out.data[6] & 0x0F);
    TEST_ASSERT_TRUE(checksumValid370(out));

    n.notifyTxResult(true, before.pendingSendAtMs);
    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_EQUAL_UINT32(1, after.sentLateEchoes);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_GREATER_THAN_INT(0, after.lastRxToTxMs);
}

void test_real_epas_inside_due_window_counts_missed_before_rescheduling()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    TEST_ASSERT_TRUE(before.pendingEcho);
    const unsigned long oldDueAt = before.pendingSendAtMs;

    n.onEpasFrame(makeEpasFrame(8), oldDueAt + 1, true);

    CanFrame out = {};
    DashEpasLateEchoDiag after = n.diag(oldDueAt + 1);
    TEST_ASSERT_EQUAL_UINT32(1, after.lateWindowMissed);
    TEST_ASSERT_FALSE(n.buildDueFrame(oldDueAt + 1, out, true, 3, nullptr));
    if (after.pendingEcho)
        TEST_ASSERT_NOT_EQUAL_UINT32(oldDueAt, after.pendingSendAtMs);
}

void test_build_due_frame_after_late_window_expires_clears_pending_and_counts_drop()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    TEST_ASSERT_TRUE(before.pendingEcho);
    const unsigned long expiredAt = before.pendingSendAtMs + DashEpasLateEcho::kMaxLatenessMs + 1;

    CanFrame out = {};
    TEST_ASSERT_FALSE(n.buildDueFrame(expiredAt, out, true, 3, nullptr));

    DashEpasLateEchoDiag after = n.diag(expiredAt);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_UINT32(1, after.droppedLateEchoes);
    TEST_ASSERT_EQUAL_STRING("lateWindowMissed", after.blockedReason);
}

void test_build_due_frame_requires_final_gate_active_at_send_time()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    CanFrame out = {};
    TEST_ASSERT_TRUE(n.due(before.pendingSendAtMs));
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out, false, 3, "finalGateLost"));

    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("finalGateLost", after.blockedReason);
}

void test_build_due_frame_requires_final_hos_active_at_send_time()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    CanFrame out = {};
    TEST_ASSERT_TRUE(n.due(before.pendingSendAtMs));
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out, true, 2, nullptr));

    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("hosClear", after.blockedReason);
}

void test_hos_clear_cancels_pending_echo()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);
    TEST_ASSERT_TRUE(n.diag(1280).pendingEcho);

    n.onDasStatus(6, 2, 1281, true, nullptr);

    DashEpasLateEchoDiag d = n.diag(1281);
    TEST_ASSERT_FALSE(d.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("hosClear", d.blockedReason);
}

void test_ap_inactive_does_not_enter_burst_on_or_schedule_echo()
{
    DashEpasLateEcho n;
    n.setEnabled(true);

    n.onDasStatus(0, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    CanFrame out = {};
    DashEpasLateEchoDiag d = n.diag(1280);
    TEST_ASSERT_EQUAL(LateEchoModeState::IDLE, d.mode);
    TEST_ASSERT_FALSE(d.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("apInactive", d.blockedReason);
    TEST_ASSERT_FALSE(n.due(1000 + 8 * 40 - DashEpasLateEcho::kLateEchoLeadMs));
    TEST_ASSERT_FALSE(n.buildDueFrame(1000 + 8 * 40 - DashEpasLateEcho::kLateEchoLeadMs, out, true, 3, nullptr));
}

void test_ap_inactive_update_cancels_existing_pending_echo()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);
    TEST_ASSERT_TRUE(n.diag(1280).pendingEcho);

    n.onDasStatus(0, 3, 1281, true, nullptr);

    CanFrame out = {};
    DashEpasLateEchoDiag d = n.diag(1281);
    TEST_ASSERT_FALSE(d.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("apInactive", d.blockedReason);
    TEST_ASSERT_FALSE(n.buildDueFrame(d.pendingSendAtMs, out, true, 3, nullptr));
}

void test_abort_state_cancels_pending_and_enters_cooldown()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);
    TEST_ASSERT_TRUE(n.diag(1280).pendingEcho);

    n.onDasStatus(8, 3, 1281, true, nullptr);

    DashEpasLateEchoDiag d = n.diag(1281);
    TEST_ASSERT_FALSE(d.pendingEcho);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, d.mode);
    TEST_ASSERT_EQUAL_STRING("abort", d.blockedReason);
}

void test_hos_clear_during_abort_cooldown_preserves_cooldown()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(8, 3, 1000, true, nullptr);

    n.onDasStatus(6, 2, 1100, true, nullptr);

    DashEpasLateEchoDiag d = n.diag(1100);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, d.mode);
    TEST_ASSERT_EQUAL_STRING("abort", d.blockedReason);
    TEST_ASSERT_GREATER_THAN_UINT32(0, d.cooldownRemainMs);
}

void test_gate_loss_during_tx_fail_cooldown_preserves_cooldown()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.notifyTxResult(false, 2000);

    n.onDasStatus(6, 3, 2100, false, "gateLost");

    DashEpasLateEchoDiag d = n.diag(2100);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, d.mode);
    TEST_ASSERT_EQUAL_STRING("txFail", d.blockedReason);
    TEST_ASSERT_GREATER_THAN_UINT32(0, d.cooldownRemainMs);
}

void test_hos_clear_during_burst_off_preserves_off_interval()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 0, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_ON, n.diag(0).mode);

    n.onDasStatus(6, 3, DashEpasLateEcho::kBurstOnMs, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, n.diag(DashEpasLateEcho::kBurstOnMs).mode);

    n.onDasStatus(6, 2, 1100, true, nullptr);
    DashEpasLateEchoDiag clear = n.diag(1100);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, clear.mode);
    TEST_ASSERT_EQUAL_STRING("hosClear", clear.blockedReason);

    n.onDasStatus(6, 3, 1200, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, n.diag(1200).mode);

    n.onDasStatus(6, 3, DashEpasLateEcho::kBurstOnMs + DashEpasLateEcho::kBurstOffMs, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_ON, n.diag(DashEpasLateEcho::kBurstOnMs + DashEpasLateEcho::kBurstOffMs).mode);
}

void test_gate_loss_during_burst_off_preserves_off_interval()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 0, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_ON, n.diag(0).mode);

    n.onDasStatus(6, 3, DashEpasLateEcho::kBurstOnMs, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, n.diag(DashEpasLateEcho::kBurstOnMs).mode);

    n.onDasStatus(6, 3, 1100, false, "gateLost");
    DashEpasLateEchoDiag gated = n.diag(1100);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, gated.mode);
    TEST_ASSERT_EQUAL_STRING("gateLost", gated.blockedReason);

    n.onDasStatus(6, 3, 1200, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_OFF, n.diag(1200).mode);

    n.onDasStatus(6, 3, DashEpasLateEcho::kBurstOnMs + DashEpasLateEcho::kBurstOffMs, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_ON, n.diag(DashEpasLateEcho::kBurstOnMs + DashEpasLateEcho::kBurstOffMs).mode);
}

void test_gate_loss_on_epas_rx_cancels_pending_echo()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);
    DashEpasLateEchoDiag before = n.diag(1280);
    TEST_ASSERT_TRUE(before.pendingEcho);

    n.onEpasFrame(makeEpasFrame(8), before.pendingSendAtMs - 1, false);

    CanFrame out = {};
    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("gate", after.blockedReason);
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out, true, 3, nullptr));
}

void test_due_window_handles_unsigned_long_rollover()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    const unsigned long lastRx = ULONG_MAX - 37UL;
    const unsigned long firstRx = lastRx - 7UL * 40UL;
    n.onDasStatus(6, 3, firstRx - 100UL, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), firstRx + i * 40UL, true);

    const unsigned long dueAt = ULONG_MAX;
    DashEpasLateEchoDiag d = n.diag(lastRx);
    TEST_ASSERT_TRUE(d.pendingEcho);
    TEST_ASSERT_EQUAL_UINT64(dueAt, d.pendingSendAtMs);
    TEST_ASSERT_FALSE(n.due(dueAt - 1UL));
    TEST_ASSERT_TRUE(n.due(dueAt));
    TEST_ASSERT_TRUE(n.due(0UL));
    TEST_ASSERT_TRUE(n.due(1UL));
    TEST_ASSERT_FALSE(n.due(2UL));
}

void test_cooldown_expiry_handles_unsigned_long_rollover()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    const unsigned long start = ULONG_MAX - 1000UL;
    n.onDasStatus(8, 3, start, true, nullptr);

    n.onDasStatus(6, 3, start + 100UL, true, nullptr);
    DashEpasLateEchoDiag during = n.diag(start + 100UL);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, during.mode);
    TEST_ASSERT_GREATER_THAN_UINT32(0, during.cooldownRemainMs);

    n.onDasStatus(6, 3, start + DashEpasLateEcho::kAbortCooldownMs, true, nullptr);
    DashEpasLateEchoDiag after = n.diag(start + DashEpasLateEcho::kAbortCooldownMs);
    TEST_ASSERT_NOT_EQUAL(LateEchoModeState::COOLDOWN, after.mode);
}

void test_cadence_tracker_recovers_after_transient_instability()
{
    DashEpasCadenceTracker t;
    const unsigned long badTimes[] = {1000, 1040, 1080, 1120, 1160, 1200, 1248, 1280};
    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(i), badTimes[i]);
    TEST_ASSERT_FALSE(t.stable());
    TEST_ASSERT_EQUAL_STRING("cadenceUnstable", t.blockedReason());

    for (uint8_t i = 0; i < 8; ++i)
        t.onRx370(makeEpasFrame(8 + i), 1320 + i * 40);

    TEST_ASSERT_TRUE(t.stable());
    TEST_ASSERT_TRUE(t.lateEchoEligible(1320 + 7 * 40));
    TEST_ASSERT_EQUAL_STRING("", t.blockedReason());
}

void test_late_echo_torque_walk_stays_same_sign_within_burst()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag first = n.diag(1280);
    CanFrame out1 = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(first.pendingSendAtMs, out1, true, 3, nullptr));
    const int tq1 = decodeSignedTorque(out1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(150, tq1 > 0 ? tq1 : -tq1);
    TEST_ASSERT_LESS_OR_EQUAL_INT(180, tq1 > 0 ? tq1 : -tq1);
    n.notifyTxResult(true, first.pendingSendAtMs);

    n.onEpasFrame(makeEpasFrame(8), 1320, true);
    DashEpasLateEchoDiag second = n.diag(1320);
    CanFrame out2 = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(second.pendingSendAtMs, out2, true, 3, nullptr));
    const int tq2 = decodeSignedTorque(out2);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(150, tq2 > 0 ? tq2 : -tq2);
    TEST_ASSERT_LESS_OR_EQUAL_INT(180, tq2 > 0 ? tq2 : -tq2);
    TEST_ASSERT_TRUE((tq1 > 0 && tq2 > 0) || (tq1 < 0 && tq2 < 0));
}

void test_late_echo_torque_direction_alternates_on_next_burst()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag first = n.diag(1280);
    CanFrame out1 = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(first.pendingSendAtMs, out1, true, 3, nullptr));
    const int tq1 = decodeSignedTorque(out1);
    n.notifyTxResult(true, first.pendingSendAtMs);

    n.onDasStatus(6, 3, 3400, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(8 + i), 3400 + i * 40, true);

    DashEpasLateEchoDiag secondBurst = n.diag(3680);
    CanFrame out2 = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(secondBurst.pendingSendAtMs, out2, true, 3, nullptr));
    const int tq2 = decodeSignedTorque(out2);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(150, tq2 > 0 ? tq2 : -tq2);
    TEST_ASSERT_LESS_OR_EQUAL_INT(180, tq2 > 0 ? tq2 : -tq2);
    TEST_ASSERT_TRUE((tq1 > 0 && tq2 < 0) || (tq1 < 0 && tq2 > 0));
}

void test_build_due_frame_is_single_use_until_tx_result()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    CanFrame out = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(before.pendingSendAtMs, out, true, 3, nullptr));
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out, true, 3, nullptr));
    n.notifyTxResult(true, before.pendingSendAtMs);
    TEST_ASSERT_EQUAL_UINT32(1, n.diag(before.pendingSendAtMs).sentLateEchoes);
}

void test_build_due_frame_rejects_pending_after_burst_on_expires()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 0, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 683 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(963);
    TEST_ASSERT_TRUE(before.pendingEcho);
    TEST_ASSERT_EQUAL_UINT32(1000, before.pendingSendAtMs);

    CanFrame out = {};
    TEST_ASSERT_FALSE(n.due(1000));
    TEST_ASSERT_FALSE(n.buildDueFrame(1000, out, true, 3, nullptr));
    DashEpasLateEchoDiag after = n.diag(1000);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_NOT_EQUAL(LateEchoModeState::BURST_ON, after.mode);
    TEST_ASSERT_EQUAL_STRING("burstOff", after.blockedReason);
}

void test_disable_preserves_abort_cooldown_until_expiry()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(8, 3, 1000, true, nullptr);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, n.diag(1000).mode);

    n.setEnabled(false);
    n.setEnabled(true);
    n.onDasStatus(6, 3, 1100, true, nullptr);

    DashEpasLateEchoDiag during = n.diag(1100);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, during.mode);
    TEST_ASSERT_EQUAL_STRING("abort", during.blockedReason);
    TEST_ASSERT_GREATER_THAN_UINT32(0, during.cooldownRemainMs);

    n.onDasStatus(6, 3, 1000 + DashEpasLateEcho::kAbortCooldownMs, true, nullptr);
    DashEpasLateEchoDiag after = n.diag(1000 + DashEpasLateEcho::kAbortCooldownMs);
    TEST_ASSERT_EQUAL(LateEchoModeState::BURST_ON, after.mode);
}

void test_disable_preserves_tx_fail_cooldown_until_expiry()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.notifyTxResult(false, 2000);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, n.diag(2000).mode);

    n.setEnabled(false);
    n.setEnabled(true);
    n.onDasStatus(6, 3, 2100, true, nullptr);

    DashEpasLateEchoDiag during = n.diag(2100);
    TEST_ASSERT_EQUAL(LateEchoModeState::COOLDOWN, during.mode);
    TEST_ASSERT_EQUAL_STRING("txFail", during.blockedReason);
    TEST_ASSERT_GREATER_THAN_UINT32(0, during.cooldownRemainMs);
}

void test_invalid_real_epas_inside_due_window_cancels_stale_pending_echo()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);
    DashEpasLateEchoDiag before = n.diag(1280);
    TEST_ASSERT_TRUE(before.pendingEcho);

    n.onEpasFrame(makeEpasFrame(12), before.pendingSendAtMs + 1, true);

    CanFrame out = {};
    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs + 1);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_EQUAL_STRING("counterUnstable", after.blockedReason);
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs + 1, out, true, 3, nullptr));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_cadence_tracker_becomes_stable_after_eight_40ms_frames);
    RUN_TEST(test_cadence_tracker_handles_counter_wrap_progression);
    RUN_TEST(test_cadence_tracker_blocks_on_jitter_above_threshold);
    RUN_TEST(test_cadence_tracker_blocks_counter_jump);
    RUN_TEST(test_encoder_preserves_source_fields_and_recomputes_checksum);
    RUN_TEST(test_encoder_clamps_torque_to_180_raw);
    RUN_TEST(test_late_echo_schedules_at_period_minus_lead);
    RUN_TEST(test_new_rx_before_due_cancels_pending_and_counts_missed_window);
    RUN_TEST(test_due_frame_builds_only_in_late_window_and_preserves_byte4);
    RUN_TEST(test_real_epas_inside_due_window_counts_missed_before_rescheduling);
    RUN_TEST(test_build_due_frame_after_late_window_expires_clears_pending_and_counts_drop);
    RUN_TEST(test_build_due_frame_requires_final_gate_active_at_send_time);
    RUN_TEST(test_build_due_frame_requires_final_hos_active_at_send_time);
    RUN_TEST(test_hos_clear_cancels_pending_echo);
    RUN_TEST(test_ap_inactive_does_not_enter_burst_on_or_schedule_echo);
    RUN_TEST(test_ap_inactive_update_cancels_existing_pending_echo);
    RUN_TEST(test_abort_state_cancels_pending_and_enters_cooldown);
    RUN_TEST(test_hos_clear_during_abort_cooldown_preserves_cooldown);
    RUN_TEST(test_gate_loss_during_tx_fail_cooldown_preserves_cooldown);
    RUN_TEST(test_hos_clear_during_burst_off_preserves_off_interval);
    RUN_TEST(test_gate_loss_during_burst_off_preserves_off_interval);
    RUN_TEST(test_gate_loss_on_epas_rx_cancels_pending_echo);
    RUN_TEST(test_due_window_handles_unsigned_long_rollover);
    RUN_TEST(test_cooldown_expiry_handles_unsigned_long_rollover);
    RUN_TEST(test_cadence_tracker_recovers_after_transient_instability);
    RUN_TEST(test_late_echo_torque_walk_stays_same_sign_within_burst);
    RUN_TEST(test_late_echo_torque_direction_alternates_on_next_burst);
    RUN_TEST(test_build_due_frame_is_single_use_until_tx_result);
    RUN_TEST(test_build_due_frame_rejects_pending_after_burst_on_expires);
    RUN_TEST(test_disable_preserves_abort_cooldown_until_expiry);
    RUN_TEST(test_disable_preserves_tx_fail_cooldown_until_expiry);
    RUN_TEST(test_invalid_real_epas_inside_due_window_cancels_stale_pending_echo);
    return UNITY_END();
}
