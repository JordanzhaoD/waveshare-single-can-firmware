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
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs - 1, out));
    TEST_ASSERT_TRUE(n.buildDueFrame(before.pendingSendAtMs, out));
    TEST_ASSERT_EQUAL_HEX8(0x20, out.data[4]);
    TEST_ASSERT_EQUAL_UINT8(8, out.data[6] & 0x0F);
    TEST_ASSERT_TRUE(checksumValid370(out));

    n.notifyTxResult(true, before.pendingSendAtMs);
    DashEpasLateEchoDiag after = n.diag(before.pendingSendAtMs);
    TEST_ASSERT_EQUAL_UINT32(1, after.sentLateEchoes);
    TEST_ASSERT_FALSE(after.pendingEcho);
    TEST_ASSERT_GREATER_THAN_INT(0, after.lastRxToTxMs);
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
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out));
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

void test_build_due_frame_is_single_use_until_tx_result()
{
    DashEpasLateEcho n;
    n.setEnabled(true);
    n.onDasStatus(6, 3, 900, true, nullptr);
    for (uint8_t i = 0; i < 8; ++i)
        n.onEpasFrame(makeEpasFrame(i), 1000 + i * 40, true);

    DashEpasLateEchoDiag before = n.diag(1280);
    CanFrame out = {};
    TEST_ASSERT_TRUE(n.buildDueFrame(before.pendingSendAtMs, out));
    TEST_ASSERT_FALSE(n.buildDueFrame(before.pendingSendAtMs, out));
    n.notifyTxResult(true, before.pendingSendAtMs);
    TEST_ASSERT_EQUAL_UINT32(1, n.diag(before.pendingSendAtMs).sentLateEchoes);
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
    RUN_TEST(test_hos_clear_cancels_pending_echo);
    RUN_TEST(test_abort_state_cancels_pending_and_enters_cooldown);
    RUN_TEST(test_hos_clear_during_abort_cooldown_preserves_cooldown);
    RUN_TEST(test_gate_loss_during_tx_fail_cooldown_preserves_cooldown);
    RUN_TEST(test_gate_loss_on_epas_rx_cancels_pending_echo);
    RUN_TEST(test_due_window_handles_unsigned_long_rollover);
    RUN_TEST(test_cooldown_expiry_handles_unsigned_long_rollover);
    RUN_TEST(test_cadence_tracker_recovers_after_transient_instability);
    RUN_TEST(test_build_due_frame_is_single_use_until_tx_result);
    return UNITY_END();
}
