#include <unity.h>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "drivers/mock_driver.h"

static MockDriver mock;
static HW4Handler handler;

static bool denyInjection()
{
    return false;
}

void setUp()
{
    mock.reset();
    handler = HW4Handler();
    handler.enablePrint = false;
    handler.emergencyVehicleDetection = true;
    handler.isaChimeSuppress = true;
}

void tearDown() {}

// --- Speed profile from follow distance (CAN ID 1016) ---

void test_hw4_follow_distance_1_sets_profile_3()
{
    CanFrame f = {.id = 1016};
    f.data[5] = 0b00100000; // fd = 1
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(3, handler.speedProfile);
}

void test_hw4_follow_distance_2_sets_profile_2()
{
    CanFrame f = {.id = 1016};
    f.data[5] = 0b01000000; // fd = 2
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(2, handler.speedProfile);
}

void test_hw4_follow_distance_3_sets_profile_1()
{
    CanFrame f = {.id = 1016};
    f.data[5] = 0b01100000; // fd = 3
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(1, handler.speedProfile);
}

void test_hw4_follow_distance_4_sets_profile_0()
{
    CanFrame f = {.id = 1016};
    f.data[5] = 0b10000000; // fd = 4
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(0, handler.speedProfile);
}

void test_hw4_follow_distance_5_sets_profile_4()
{
    CanFrame f = {.id = 1016};
    f.data[5] = 0b10100000; // fd = 5
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(4, handler.speedProfile);
}

void test_hw4_manual_profile_ignores_follow_distance()
{
    handler.speedProfileAuto = false;
    handler.speedProfile = 1;

    CanFrame f = {.id = 1016};
    f.data[5] = 0b00100000; // fd = 1 would map to profile 3
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL_INT(1, handler.speedProfile);
    TEST_ASSERT_FALSE(handler.speedProfileAuto);
}

// --- AD shadowing fix regression test ---

void test_hw4_AD_enabled_only_set_on_mux0()
{
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);

    mock.reset();
    CanFrame f2 = {.id = 1021};
    f2.data[0] = 0x02;
    f2.data[4] = 0x00; // AD bit not set in mux 2
    handler.handleMessage(f2, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(1, mock.sent.size()); // mux 2 now sends when fsdTriggered
}

// --- AD activation (mux 0) ---

void test_hw4_AD_mux0_sets_bits_46_and_60()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40); // bit 46
    TEST_ASSERT_EQUAL_HEX8(0x10, mock.sent[0].data[7] & 0x10); // bit 60
}

void test_hw4_AD_mux0_sets_emergency_bit59()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x08, mock.sent[0].data[7] & 0x08); // bit 59
}

void test_hw4_AD_mux0_skips_emergency_bit59_when_disabled()
{
    handler.emergencyVehicleDetection = false;
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, mock.sent[0].data[7] & 0x08);
}

void test_hw4_no_send_when_AD_disabled_mux0()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x00;
    handler.handleMessage(f, mock);
    TEST_ASSERT_FALSE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_checkAD_blocks_mux0_and_mux2_send()
{
    handler.checkAD = denyInjection;

    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    // ADEnabled tracks the FSD UI trigger (data[4] bit6) and is independent of the
    // injection gate — checkAD gates the actual SEND, not the trigger flag.
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size()); // mux0 send blocked by checkAD

    mock.reset();
    handler.ADEnabled = true;
    CanFrame f2 = {.id = 1021};
    f2.data[0] = 0x02;
    handler.handleMessage(f2, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size()); // mux2 send blocked by checkAD
}

// --- TLSSC bypass (bit 38 on mux 0) ---

void test_hw4_tlsscBypass_sets_bit38_on_mux0()
{
    handler.tlsscBypass = true;
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[4] & 0x40); // bit 38
}

// --- Nag suppression (mux 1) ---

void test_hw4_nag_suppression_clears_bit19_sets_bit47()
{
    // Trigger fsd via mux 0 first
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);     // bit 19 cleared
    TEST_ASSERT_EQUAL_HEX8(0x80, mock.sent[0].data[5] & 0x80); // bit 47 set
}

void test_hw4_mux1_sends_0_when_fsd_not_triggered()
{
    // No mux 0 trigger, so fsdTriggered is false
    handler.ADEnabled = true;
    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// --- Mux 2: always injects speed profile + offset when fsdTriggered ---

void test_hw4_mux2_injects_speed_profile_when_triggered()
{
    handler.speedProfile = 3;
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    f.data[7] = 0x00;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x30, mock.sent[0].data[7] & 0x70); // profile 3 = 0x03 << 4
}

void test_hw4_mux2_preserves_profile_bits()
{
    handler.speedProfile = 0;
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    f.data[7] = 0x70; // old profile bits all set
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, mock.sent[0].data[7] & 0x70); // profile 0 clears all
}

// --- Send counts ---

void test_hw4_mux0_AD_enabled_sends_1()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_hw4_mux1_sends_1()
{
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_hw4_mux2_sends_1_when_triggered()
{
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_hw4_manual_profile_injects_mux2_speed_profile()
{
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();

    handler.speedProfileAuto = false;
    handler.speedProfile = 4;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    f.data[7] = 0x70;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[7] & 0x70); // profile 4 = 0x04 << 4
}

void test_hw4_ignores_unrelated_can_id()
{
    CanFrame f = {.id = 999};
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// --- Mux 2 offset ---

void test_hw4_mux2_writes_offset()
{
    handler.hw4OffsetRaw = 10;
    // Trigger fsd via mux 0
    CanFrame f0 = {.id = 1021};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);
    mock.reset();
    // Mux 2
    CanFrame f2 = {.id = 1021};
    f2.data[0] = 0x02;
    handler.handleMessage(f2, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(10, mock.sent[0].data[1] & 0x3F);
}

// --- ISA speed chime suppression (CAN ID 921) ---

void test_hw4_isa_suppress_sets_bit5_of_data1()
{
    CanFrame f = {.id = 921};
    f.data[1] = 0x00;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x20, mock.sent[0].data[1] & 0x20);
}

void test_hw4_isa_suppress_preserves_existing_data1_bits()
{
    CanFrame f = {.id = 921};
    f.data[1] = 0xC3;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xE3, mock.sent[0].data[1]); // 0xC3 | 0x20
}

void test_hw4_isa_suppress_checksum_correct()
{
    CanFrame f = {.id = 921};
    f.data[0] = 0x10;
    f.data[1] = 0x05;
    f.data[2] = 0x00;
    f.data[3] = 0x00;
    f.data[4] = 0x00;
    f.data[5] = 0x00;
    f.data[6] = 0x00;
    handler.handleMessage(f, mock);
    // After OR: data[1] = 0x25
    // computeVehicleChecksum: sum of (id_lo + id_hi) + data[0..6] (skip byte 7)
    // = 0x99 + 0x03 + 0x10 + 0x25 + 0 + 0 + 0 + 0 = 0xD1
    TEST_ASSERT_EQUAL_HEX8(0xD1, mock.sent[0].data[7]);
}

void test_hw4_isa_suppress_returns_early_no_further_processing()
{
    handler.ADEnabled = true;
    CanFrame f = {.id = 921};
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size()); // only the ISA send, not any AD logic
}

void test_hw4_isa_suppress_runtime_off_skips_send()
{
    handler.isaChimeSuppress = false;
    CanFrame f = {.id = 921};
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_das_status_available_does_not_mark_ap_active()
{
    CanFrame f = {.id = 921};
    f.data[0] = 0x02; // AVAILABLE

    handler.handleMessage(f, mock);

    TEST_ASSERT_FALSE(handler.APActive);
}

void test_hw4_das_status_active_marks_ap_active()
{
    CanFrame f = {.id = 921};
    f.data[0] = 0x04; // ACTIVE_2

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.APActive);
}

void test_hw4_gw_autopilot_mux2_updates_state_without_send()
{
    CanFrame f = {.id = 2047};
    f.data[0] = 0x02;
    f.data[5] = 0x0C; // SELF_DRIVING = 3
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(3, handler.gatewayAutopilot);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_gear_park_marks_parked()
{
    CanFrame f = {.id = 390};
    f.dlc = 8;
    f.data[7] = static_cast<uint8_t>(1U << 3);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.Parked);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_gear_drive_clears_parked()
{
    handler.Parked = true;
    CanFrame f = {.id = 390};
    f.dlc = 8;
    f.data[7] = static_cast<uint8_t>(4U << 3);

    handler.handleMessage(f, mock);

    TEST_ASSERT_FALSE(handler.Parked);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// --- Filter IDs ---

void test_hw4_filter_ids_count()
{
    TEST_ASSERT_EQUAL_UINT8(10, handler.filterIdCount());
}

void test_hw4_filter_ids_values()
{
    const uint32_t *ids = handler.filterIds();
    TEST_ASSERT_EQUAL_UINT32(280, ids[0]);
    TEST_ASSERT_EQUAL_UINT32(390, ids[1]);
    TEST_ASSERT_EQUAL_UINT32(880, ids[2]); // 0x370 EPAS3P_sysStatus (EPAS-faithful nag)
    TEST_ASSERT_EQUAL_UINT32(920, ids[3]);
    TEST_ASSERT_EQUAL_UINT32(921, ids[4]);
    TEST_ASSERT_EQUAL_UINT32(923, ids[5]); // 0x39B DAS_status Highland/HW4
    TEST_ASSERT_EQUAL_UINT32(1016, ids[6]);
    TEST_ASSERT_EQUAL_UINT32(1021, ids[7]);
    TEST_ASSERT_EQUAL_UINT32(2047, ids[8]);
    TEST_ASSERT_EQUAL_UINT32(CAN_ID_OTA_STATUS, ids[9]);
}

// --- ISA override rewrite speed limit, HW4 (CAN ID 923 / 0x39B) — spec Task 3 ---

void test_hw4_isa_override_sets_fused_limit_to_none()
{
    handler.isaOverride = true;
    CanFrame f = {.id = 923};
    f.data[1] = 0x00;
    f.data[2] = 0x00;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x1F, mock.sent[0].data[1] & 0x1F);
    TEST_ASSERT_EQUAL_HEX8(0x1F, mock.sent[0].data[2] & 0x1F);
}

void test_hw4_isa_override_preserves_high_bits()
{
    handler.isaOverride = true;
    CanFrame f = {.id = 923};
    f.data[1] = 0xE0;
    f.data[2] = 0xE0;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mock.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, mock.sent[0].data[2]);
}

void test_hw4_isa_override_checksum_correct()
{
    handler.isaOverride = true;
    CanFrame f = {.id = 923};
    f.data[0] = 0x10;
    f.data[1] = 0x00;
    f.data[2] = 0x00;
    f.data[3] = 0;
    f.data[4] = 0;
    f.data[5] = 0;
    f.data[6] = 0;
    handler.handleMessage(f, mock);
    // OR 后 data[1]=0x1F, data[2]=0x1F; checksum = 0x9B+0x03+0x10+0x1F+0x1F = 0xEC
    TEST_ASSERT_EQUAL_HEX8(0xEC, mock.sent[0].data[7]);
}

void test_hw4_isa_override_runtime_off_skips_send()
{
    handler.isaOverride = false;
    CanFrame f = {.id = 923};
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_923_standard_byte1_abort_latches_and_blocks_isa_override()
{
    handler.abortGuard.setEnabled(true);
    handler.isaOverride = true;
    CanFrame f = {.id = 923};
    f.data[0] = 0x00;
    f.data[1] = 0x80; // standard HW4 DAS_autopilotState = 8 in byte1[7:4]

    handler.handleMessage(f, mock);

    DashAbortGuardDiag diag = handler.abortGuard.diag();
    TEST_ASSERT_TRUE(diag.latched);
    TEST_ASSERT_EQUAL_UINT8(8, diag.lastAbortState);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_STRING("hw4_das_status_923", diag.lastBlockedPath);
}

void test_hw4_923_standard_byte0_abort_nibble_does_not_latch_when_byte1_moved()
{
    handler.abortGuard.setEnabled(true);
    handler.isaOverride = false;
    CanFrame f = {.id = 923};
    f.data[0] = 0x08; // unrelated byte0 low nibble must not be treated as abort on standard HW4
    f.data[1] = 0x20; // byte1[7:4] != 1 proves standard HW4 byte1 mapping

    handler.handleMessage(f, mock);

    DashAbortGuardDiag diag = handler.abortGuard.diag();
    TEST_ASSERT_FALSE(diag.latched);
    TEST_ASSERT_EQUAL_UINT8(2, diag.lastApState);
}

void test_hw4_923_highland_byte0_fallback_requires_three_pinned_byte1_frames()
{
    handler.abortGuard.setEnabled(true);
    handler.isaOverride = false;
    CanFrame f = {.id = 923};
    f.data[0] = 0x08; // Highland fallback AP abort state candidate
    f.data[1] = 0x10; // byte1[7:4] pinned at 1 signature

    handler.handleMessage(f, mock);
    TEST_ASSERT_FALSE(handler.abortGuard.diag().latched);
    handler.handleMessage(f, mock);
    TEST_ASSERT_FALSE(handler.abortGuard.diag().latched);
    handler.handleMessage(f, mock);

    DashAbortGuardDiag diag = handler.abortGuard.diag();
    TEST_ASSERT_TRUE(diag.latched);
    TEST_ASSERT_EQUAL_UINT8(8, diag.lastAbortState);
}

void test_hw4_923_fallback_exits_when_byte1_moves_again()
{
    handler.abortGuard.setEnabled(true);
    handler.isaOverride = false;
    CanFrame highland = {.id = 923};
    highland.data[0] = 0x02;
    highland.data[1] = 0x10; // byte1 pinned; after 3 frames fallback uses byte0
    handler.handleMessage(highland, mock);
    handler.handleMessage(highland, mock);
    handler.handleMessage(highland, mock);
    TEST_ASSERT_TRUE(handler.hw4Das923UseByte0);

    CanFrame standardAbort = {.id = 923};
    standardAbort.data[0] = 0x02; // non-abort byte0 must be ignored after byte1 moves
    standardAbort.data[1] = 0x80; // standard HW4 abort state in byte1[7:4]
    handler.handleMessage(standardAbort, mock);

    DashAbortGuardDiag diag = handler.abortGuard.diag();
    TEST_ASSERT_TRUE(diag.latched);
    TEST_ASSERT_EQUAL_UINT8(8, diag.lastAbortState);
    TEST_ASSERT_FALSE(handler.hw4Das923UseByte0);
}

void test_hw4_923_highland_fallback_requires_consecutive_candidate_frames()
{
    handler.abortGuard.setEnabled(true);
    handler.isaOverride = false;
    CanFrame f = {.id = 923};
    f.data[1] = 0x10; // byte1 pinned signature

    f.data[0] = 0x08;
    handler.handleMessage(f, mock);
    f.data[0] = 0x00;
    handler.handleMessage(f, mock);
    f.data[0] = 0x08;
    handler.handleMessage(f, mock);
    f.data[0] = 0x00;
    handler.handleMessage(f, mock);
    f.data[0] = 0x08;
    handler.handleMessage(f, mock);

    DashAbortGuardDiag diag = handler.abortGuard.diag();
    TEST_ASSERT_FALSE(handler.hw4Das923UseByte0);
    TEST_ASSERT_FALSE(diag.latched);
    TEST_ASSERT_EQUAL_UINT8(1, diag.lastApState);
}

// --- Ban Shield ---

void test_hw4_ban_shield_blocks_changed_2047_mux2()
{
    handler.banShieldEnable = true;
    // Learn baseline
    CanFrame learn = {.id = 2047};
    learn.data[0] = 0x02;
    learn.dlc = 8;
    learn.data[5] = 0x08;
    handler.handleMessage(learn, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    // Changed frame should be blocked
    mock.reset();
    CanFrame attack = {.id = 2047};
    attack.data[0] = 0x02;
    attack.dlc = 8;
    attack.data[5] = 0x0C;
    handler.handleMessage(attack, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x08, mock.sent[0].data[5]);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)handler.banShieldBlocks);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_hw4_filter_ids_count);
    RUN_TEST(test_hw4_filter_ids_values);

    RUN_TEST(test_hw4_follow_distance_1_sets_profile_3);
    RUN_TEST(test_hw4_follow_distance_2_sets_profile_2);
    RUN_TEST(test_hw4_follow_distance_3_sets_profile_1);
    RUN_TEST(test_hw4_follow_distance_4_sets_profile_0);
    RUN_TEST(test_hw4_follow_distance_5_sets_profile_4);
    RUN_TEST(test_hw4_manual_profile_ignores_follow_distance);

    RUN_TEST(test_hw4_AD_enabled_only_set_on_mux0);
    RUN_TEST(test_hw4_AD_mux0_sets_bits_46_and_60);
    RUN_TEST(test_hw4_AD_mux0_sets_emergency_bit59);
    RUN_TEST(test_hw4_AD_mux0_skips_emergency_bit59_when_disabled);
    RUN_TEST(test_hw4_no_send_when_AD_disabled_mux0);
    RUN_TEST(test_hw4_checkAD_blocks_mux0_and_mux2_send);
    RUN_TEST(test_hw4_tlsscBypass_sets_bit38_on_mux0);

    RUN_TEST(test_hw4_nag_suppression_clears_bit19_sets_bit47);
    RUN_TEST(test_hw4_mux1_sends_0_when_fsd_not_triggered);

    RUN_TEST(test_hw4_mux2_injects_speed_profile_when_triggered);
    RUN_TEST(test_hw4_mux2_preserves_profile_bits);

    RUN_TEST(test_hw4_mux0_AD_enabled_sends_1);
    RUN_TEST(test_hw4_mux1_sends_1);
    RUN_TEST(test_hw4_mux2_sends_1_when_triggered);
    RUN_TEST(test_hw4_manual_profile_injects_mux2_speed_profile);
    RUN_TEST(test_hw4_ignores_unrelated_can_id);
    RUN_TEST(test_hw4_mux2_writes_offset);

    RUN_TEST(test_hw4_isa_suppress_sets_bit5_of_data1);
    RUN_TEST(test_hw4_isa_suppress_preserves_existing_data1_bits);
    RUN_TEST(test_hw4_isa_suppress_checksum_correct);
    RUN_TEST(test_hw4_isa_suppress_returns_early_no_further_processing);
    RUN_TEST(test_hw4_isa_suppress_runtime_off_skips_send);
    RUN_TEST(test_hw4_das_status_available_does_not_mark_ap_active);
    RUN_TEST(test_hw4_das_status_active_marks_ap_active);
    RUN_TEST(test_hw4_gw_autopilot_mux2_updates_state_without_send);
    RUN_TEST(test_hw4_gear_park_marks_parked);
    RUN_TEST(test_hw4_gear_drive_clears_parked);

    RUN_TEST(test_hw4_ban_shield_blocks_changed_2047_mux2);

    RUN_TEST(test_hw4_isa_override_sets_fused_limit_to_none);
    RUN_TEST(test_hw4_isa_override_preserves_high_bits);
    RUN_TEST(test_hw4_isa_override_checksum_correct);
    RUN_TEST(test_hw4_isa_override_runtime_off_skips_send);
    RUN_TEST(test_hw4_923_standard_byte1_abort_latches_and_blocks_isa_override);
    RUN_TEST(test_hw4_923_standard_byte0_abort_nibble_does_not_latch_when_byte1_moved);
    RUN_TEST(test_hw4_923_highland_byte0_fallback_requires_three_pinned_byte1_frames);
    RUN_TEST(test_hw4_923_fallback_exits_when_byte1_moves_again);
    RUN_TEST(test_hw4_923_highland_fallback_requires_consecutive_candidate_frames);

    return UNITY_END();
}
