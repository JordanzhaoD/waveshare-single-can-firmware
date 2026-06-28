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

void test_encoder_preserves_byte4_and_recomputes_checksum()
{
    CanFrame source = makeEpasFrame(7, 0x20, -12);
    CanFrame out = {};

    TEST_ASSERT_TRUE(DashEpasFaithfulEncoder::build(source, 8, 170, out));

    TEST_ASSERT_EQUAL_UINT32(880, out.id);
    TEST_ASSERT_EQUAL_UINT8(8, out.dlc);
    TEST_ASSERT_EQUAL_HEX8(source.data[4], out.data[4]);
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

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_cadence_tracker_becomes_stable_after_eight_40ms_frames);
    RUN_TEST(test_cadence_tracker_blocks_on_jitter_above_threshold);
    RUN_TEST(test_cadence_tracker_blocks_counter_jump);
    RUN_TEST(test_encoder_preserves_byte4_and_recomputes_checksum);
    RUN_TEST(test_encoder_clamps_torque_to_180_raw);
    RUN_TEST(test_late_echo_schedules_at_period_minus_lead);
    RUN_TEST(test_new_rx_before_due_cancels_pending_and_counts_missed_window);
    RUN_TEST(test_due_frame_builds_only_in_late_window_and_preserves_byte4);
    RUN_TEST(test_hos_clear_cancels_pending_echo);
    RUN_TEST(test_abort_state_cancels_pending_and_enters_cooldown);
    return UNITY_END();
}
