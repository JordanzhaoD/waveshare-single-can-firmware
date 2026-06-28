#include <unity.h>
#include <cstring>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "drivers/mock_driver.h"

static MockDriver mock;
static LegacyHandler handler;

static bool denyAD()
{
    return false;
}

static FsdGateBlockReason denyByApGate()
{
    return FsdGateBlockReason::ApGate;
}

// Helper: build a realistic CAN 880 (0x370 EPAS3P_sysStatus) frame.
static CanFrame makeEpasFrame(uint8_t handsOn, float torqueNm, uint8_t counter)
{
    CanFrame f = {.id = 880, .dlc = 8};
    f.data[0] = 0x12;
    f.data[1] = 0x00;
    uint16_t tRaw = static_cast<uint16_t>((torqueNm + 20.5) / 0.01);
    f.data[2] = static_cast<uint8_t>(0x08 | ((tRaw >> 8) & 0x0F));
    f.data[3] = static_cast<uint8_t>(tRaw & 0xFF);
    f.data[4] = static_cast<uint8_t>(((handsOn & 0x03) << 6) | 0x1F);
    f.data[5] = 0x89;
    f.data[6] = static_cast<uint8_t>((2 << 5) | (counter & 0x0F));
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return f;
}

static int32_t decodeEchoTorqueRaw(const CanFrame &f)
{
    return static_cast<int32_t>(((f.data[2] & 0x0F) << 8) | f.data[3]);
}

static int32_t decodeEchoTorqueSigned(const CanFrame &f)
{
    return decodeEchoTorqueRaw(f) - 0x800;
}

static uint8_t decodeEchoHandsOnLevel(const CanFrame &f)
{
    return static_cast<uint8_t>((f.data[4] >> 6) & 0x03);
}

static bool checksumValid370(const CanFrame &f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += f.data[i];
    return static_cast<uint8_t>((sum + 0x73) & 0xFF) == f.data[7];
}

static uint8_t counterLowNibble(const CanFrame &f)
{
    return static_cast<uint8_t>(f.data[6] & 0x0F);
}

static CanFrame makeDasFrame(uint8_t handsOnState);

static CanFrame makeDasFrameWithState(uint8_t handsOnState, uint8_t apState)
{
    CanFrame f = makeDasFrame(handsOnState);
    f.data[0] = static_cast<uint8_t>(apState & 0x0F);
    return f;
}

// Helper: build 0x399 (921 DAS_status) frame. handsOnState → data[5] bits[5:2].
static CanFrame makeDasFrame(uint8_t handsOnState)
{
    CanFrame f = {.id = 921, .dlc = 8};
    f.data[0] = 0x04; // AP active (isDASAutopilotActive accepts byte0 in {3,4,5})
    f.data[1] = 0x00;
    f.data[2] = 0;
    f.data[3] = 0;
    f.data[4] = 0;
    f.data[5] = static_cast<uint8_t>((handsOnState & 0x0F) << 2);
    f.data[6] = 0;
    f.data[7] = 0;
    return f;
}

void setUp()
{
    mock.reset();
    handler = LegacyHandler();
    handler.enablePrint = false;
    fusedSpeedLimitRaw = 0;
    offsetMode = 1;
    manualOffsetPct = 0;
    customPct[0] = 30;
    customPct[1] = 20;
    customPct[2] = 10;
    customPct[3] = 10;
    smoothedOffset = 0.0f;
    actualOffset = 0.0f;
    forceActivateRuntime = false;
    handler.bionicSteering = false; // default = bionic OFF; bionic tests opt in explicitly
    handler.checkAD = nullptr;      // default: no AD gate; checkAD test opts in explicitly
}

void tearDown() {}

// --- Speed profile from stalk position (CAN ID 69) ---

void test_legacy_stalk_pos0_sets_profile_2()
{
    CanFrame f = {.id = 69};
    f.data[1] = 0x00; // pos = 0 >> 5 = 0
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(2, handler.speedProfile);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_stalk_pos1_sets_profile_2()
{
    CanFrame f = {.id = 69};
    f.data[1] = 0x21; // pos = 0x21 >> 5 = 1
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(2, handler.speedProfile);
}

void test_legacy_stalk_pos2_sets_profile_1()
{
    CanFrame f = {.id = 69};
    f.data[1] = 0x42; // pos = 0x42 >> 5 = 2
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(1, handler.speedProfile);
}

void test_legacy_stalk_pos3_sets_profile_0()
{
    CanFrame f = {.id = 69};
    f.data[1] = 0x64; // pos = 0x64 >> 5 = 3
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_INT(0, handler.speedProfile);
}

void test_legacy_manual_profile_ignores_stalk_position()
{
    handler.speedProfileAuto = false;
    handler.speedProfile = 1;

    CanFrame f = {.id = 69};
    f.data[1] = 0x00; // would map to profile 2
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL_INT(1, handler.speedProfile);
    TEST_ASSERT_FALSE(handler.speedProfileAuto);
}

// --- AD activation (CAN ID 1006) ---

void test_legacy_AD_enabled_on_mux0()
{
    CanFrame f = {.id = 1006};
    f.data[0] = 0x00; // mux 0
    f.data[4] = 0x40; // FSD bit set (bit 38 = bit 6 of byte 4)
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_legacy_no_send_when_AD_disabled()
{
    CanFrame f = {.id = 1006};
    f.data[0] = 0x00; // mux 0
    f.data[4] = 0x00; // FSD bit NOT set
    handler.handleMessage(f, mock);
    TEST_ASSERT_FALSE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_AD_sets_bit46()
{
    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
}

void test_legacy_stable_mux0_sets_bit46_only()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::Stable;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    f.data[6] = 0x02;
    uint8_t before[8];
    memcpy(before, f.data, 8);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
    TEST_ASSERT_EQUAL_HEX8(before[6], mock.sent[0].data[6]);
    for (int i = 0; i < 8; ++i)
    {
        if (i == 5)
            continue;
        TEST_ASSERT_EQUAL_HEX8(before[i], mock.sent[0].data[i]);
    }
    TEST_ASSERT_EQUAL(FsdSkipReason::Sent, handler.legacyFsdDiag.mux0.lastSkip);
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacyFsdDiag.mux0.tx);
}

void test_legacy_checkAD_blocks_mux0_send()
{
    handler.checkAD = denyAD;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// --- fsdTriggered state tracking ---

void test_legacy_fsdTriggered_set_on_mux0()
{
    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
}

// --- Nag suppression (mux 1) ---

void test_legacy_stable_mux1_is_disabled()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::Stable;

    CanFrame f0 = {.id = 1006};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);

    CanFrame f1 = {.id = 1006};
    f1.data[0] = 0x01;
    setBit(f1, 19, true);
    handler.handleMessage(f1, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacyFsdDiag.mux1.rx);
    TEST_ASSERT_EQUAL_UINT32(0, handler.legacyFsdDiag.mux1.tx);
    TEST_ASSERT_EQUAL(FsdSkipReason::DisabledInStable, handler.legacyFsdDiag.mux1.lastSkip);
}

void test_legacy_experimental_mux0_can_write_speed_profile()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::Experimental;
    handler.legacyFsdDiag.profileWriteEnable = true;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    f.data[6] = 0x02;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x04, mock.sent[0].data[6] & 0x06);
}

void test_legacy_experimental_mux1_can_clear_bit19()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::Experimental;
    handler.legacyFsdDiag.mux1Enable = true;

    CanFrame f0 = {.id = 1006};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);

    CanFrame f1 = {.id = 1006};
    f1.data[0] = 0x01;
    setBit(f1, 19, true);
    handler.handleMessage(f1, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[1].data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL(FsdSkipReason::Sent, handler.legacyFsdDiag.mux1.lastSkip);
}

void test_legacy_tesla_parity_mux0_writes_speed_profile()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::TeslaParity;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;
    f.data[6] = 0x00;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
    TEST_ASSERT_EQUAL_HEX8(0x04, mock.sent[0].data[6] & 0x06);
}

void test_legacy_tesla_parity_mux1_clears_bit19()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::TeslaParity;

    CanFrame f0 = {.id = 1006};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);

    CanFrame f1 = {.id = 1006};
    f1.data[0] = 0x01;
    setBit(f1, 19, true);
    handler.handleMessage(f1, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[1].data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL(FsdSkipReason::Sent, handler.legacyFsdDiag.mux1.lastSkip);
}

// Pre-fix this test asserted bit48 was PRESERVED under TeslaParity even with
// visionLimitClearEnable=true (because line 465 required policy==Experimental).
// Spec Task 1 made visionLimitClearEnable policy-independent, so the old
// expectation is inverted. The post-fix behavior is covered by
// test_legacy_vision_clear_works_under_tesla_parity_policy and
// test_legacy_vision_clear_disabled_does_not_clear_bit48 below.

void test_legacy_trigger_source_force_vs_ui_bit_vs_false()
{
    forceActivateRuntime = true;
    CanFrame forced = {.id = 1006};
    forced.data[0] = 0x00;
    forced.data[4] = 0x00;
    handler.handleMessage(forced, mock);
    TEST_ASSERT_EQUAL(FsdTriggerSource::Force, handler.legacyFsdDiag.triggerSource);

    setUp();
    CanFrame ui = {.id = 1006};
    ui.data[0] = 0x00;
    ui.data[4] = 0x40;
    handler.handleMessage(ui, mock);
    TEST_ASSERT_EQUAL(FsdTriggerSource::UiBit, handler.legacyFsdDiag.triggerSource);

    setUp();
    CanFrame none = {.id = 1006};
    none.data[0] = 0x00;
    none.data[4] = 0x00;
    handler.handleMessage(none, mock);
    TEST_ASSERT_EQUAL(FsdTriggerSource::FalseSource, handler.legacyFsdDiag.triggerSource);
    TEST_ASSERT_EQUAL(FsdSkipReason::NotTriggered, handler.legacyFsdDiag.mux0.lastSkip);
}

void test_legacy_mux0_records_can_a_twai_labels()
{
    CanFrame f = {.id = 1006};
    f.bus = CAN_BUS_VEH;
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(FsdDiagBus::CanA, handler.legacyFsdDiag.mux0.bus);
    TEST_ASSERT_EQUAL(FsdDiagDriver::Twai, handler.legacyFsdDiag.mux0.driver);
    TEST_ASSERT_NOT_EQUAL(0, handler.legacyFsdDiag.firstMux0RxMs);
    TEST_ASSERT_NOT_EQUAL(0, handler.legacyFsdDiag.firstTxOkMs);
}

void test_legacy_gate_block_records_gate_blocked_health()
{
    handler.checkAD = denyAD;
    CanFrame f = {.id = 1006};
    f.bus = CAN_BUS_VEH;
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, handler.legacyFsdDiag.mux0.lastSkip);
    TEST_ASSERT_EQUAL(FsdHealthState::GateBlocked, handler.legacyFsdDiag.health);
    TEST_ASSERT_EQUAL(FsdGateBlockReason::CheckAd, handler.legacyFsdDiag.lastBlockedBy);
}

void test_legacy_gate_block_uses_resolver_reason()
{
    handler.checkAD = denyAD;
    handler.gateBlockReason = denyByApGate;
    CanFrame f = {.id = 1006};
    f.bus = CAN_BUS_VEH;
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, handler.legacyFsdDiag.mux0.lastSkip);
    TEST_ASSERT_EQUAL(FsdHealthState::GateBlocked, handler.legacyFsdDiag.health);
    TEST_ASSERT_EQUAL(FsdGateBlockReason::ApGate, handler.legacyFsdDiag.lastBlockedBy);
}

// --- 视觉限速 clear 独立于 policy (spec Task 1) ---
// NOTE: Stable policy short-circuits mux-1 at handlers.h:431 (DisabledInStable),
// so the effective demonstration of policy-independent visionLimitClearEnable
// uses TeslaParity (which already permits mux-1 injection). The old behavior
// (Experimental-only gate) was recorded by test_legacy_tesla_parity_does_not_clear_bit48_...

void test_legacy_vision_clear_works_under_tesla_parity_policy()
{
    // Pre-fix: line 465 required policy==Experimental, so TeslaParity+vision=true
    // did NOT clear bit48. Post-fix: visionLimitClearEnable gates independently.
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::TeslaParity;
    handler.legacyFsdDiag.visionLimitClearEnable = true;

    // mux-0 first to arm fsdTriggered (matches existing TeslaParity test pattern)
    CanFrame f0 = {.id = 1006};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);

    CanFrame f1 = {.id = 1006};
    f1.data[0] = 0x01; // mux 1
    setBit(f1, 19, true);
    setBit(f1, 48, true); // bit48 set, expect cleared post-fix
    handler.handleMessage(f1, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[1].data[2] >> 3) & 0x01); // bit19 cleared (existing behavior)
    TEST_ASSERT_FALSE(mock.sent[1].data[6] & 0x01);        // bit48 cleared (NEW: post-fix)
    TEST_ASSERT_EQUAL(FsdSkipReason::Sent, handler.legacyFsdDiag.mux1.lastSkip);
}

void test_legacy_vision_clear_disabled_does_not_clear_bit48()
{
    handler.legacyFsdDiag.policy = LegacyFsdPolicy::TeslaParity;
    handler.legacyFsdDiag.visionLimitClearEnable = false;

    CanFrame f0 = {.id = 1006};
    f0.data[0] = 0x00;
    f0.data[4] = 0x40;
    handler.handleMessage(f0, mock);

    CanFrame f1 = {.id = 1006};
    f1.data[0] = 0x01;
    setBit(f1, 19, true);
    setBit(f1, 48, true);
    handler.handleMessage(f1, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[1].data[2] >> 3) & 0x01); // bit19 still cleared
    TEST_ASSERT_TRUE(mock.sent[1].data[6] & 0x01);         // bit48 PRESERVED (flag off)
}

// --- CAN 760 offset write ---

void test_legacy_can760_writes_offset()
{
    handler.legacyOffset = 10;
    CanFrame f = {.id = 760};
    f.dlc = 8;
    f.data[5] = 0xC0;
    const uint8_t originalByte5 = f.data[5];

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0xC0 | 40, mock.sent[0].data[5]);
    TEST_ASSERT_NOT_EQUAL(originalByte5, mock.sent[0].data[5]);
    TEST_ASSERT_EQUAL_HEX8(originalByte5, handler.legacyFsdDiag.aux760.before[5]);
    TEST_ASSERT_EQUAL_HEX8(mock.sent[0].data[5], handler.legacyFsdDiag.aux760.after[5]);
}

void test_legacy_can760_skips_when_offset_zero()
{
    handler.legacyOffset = 0;
    CanFrame f = {.id = 760};
    f.dlc = 8;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_can921_captures_fused_speed_limit()
{
    CanFrame f = {.id = 921};
    f.dlc = 2;
    f.data[1] = 12;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT8(12, fusedSpeedLimitRaw);
}

void test_legacy_can760_fixed_pct_writes_simple_offset()
{
    fusedSpeedLimitRaw = 12; // 60 kph
    offsetMode = 0;
    manualOffsetPct = 20;
    handler.legacyOffset = 0;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    f.data[5] = 0xC0;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0xC0 | 42, mock.sent[0].data[5]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.0f, actualOffset);
}

void test_legacy_can760_auto_writes_simple_offset()
{
    fusedSpeedLimitRaw = 12; // 60 kph -> target 90, offset 30
    offsetMode = 1;
    handler.legacyOffset = 0;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    f.data[5] = 0x80;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x80 | 60, mock.sent[0].data[5]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, actualOffset);
}

void test_legacy_can760_custom_writes_simple_offset()
{
    fusedSpeedLimitRaw = 16; // 80 kph
    offsetMode = 2;
    customPct[2] = 10;
    handler.legacyOffset = 0;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(38, mock.sent[0].data[5] & 0x3F);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 8.0f, actualOffset);
}

void test_legacy_can760_clamps_simple_offset_to_wire_max()
{
    fusedSpeedLimitRaw = 20; // 100 kph
    offsetMode = 0;
    manualOffsetPct = 50;
    handler.legacyOffset = 0;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(63, mock.sent[0].data[5] & 0x3F);
}

void test_legacy_can760_checkAD_blocks_computed_offset()
{
    fusedSpeedLimitRaw = 12;
    offsetMode = 0;
    manualOffsetPct = 20;
    handler.checkAD = denyAD;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_can760_records_tx_fail()
{
    handler.legacyOffset = 10;
    mock.sendOk = false;

    CanFrame f = {.id = 760};
    f.dlc = 8;
    f.data[5] = 0xC0;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacyFsdDiag.aux760.err);
    TEST_ASSERT_EQUAL(FsdSkipReason::TxFail, handler.legacyFsdDiag.aux760.lastSkip);
}

// --- CAN 1080 visionSpeedSlider override ---

void test_legacy_can1080_sets_vision_slider()
{
    handler.overrideSpeedLimit = true;
    CanFrame f = {.id = 1080};
    f.dlc = 8;
    f.data[7] = 0x80;
    const uint8_t originalByte7 = f.data[7];

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x80 | 100, mock.sent[0].data[7]);
    TEST_ASSERT_NOT_EQUAL(originalByte7, mock.sent[0].data[7]);
    TEST_ASSERT_EQUAL_HEX8(originalByte7, handler.legacyFsdDiag.aux1080.before[7]);
    TEST_ASSERT_EQUAL_HEX8(mock.sent[0].data[7], handler.legacyFsdDiag.aux1080.after[7]);
}

void test_legacy_can1080_skips_when_disabled()
{
    handler.overrideSpeedLimit = false;
    CanFrame f = {.id = 1080};
    f.dlc = 8;
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_can1080_records_tx_fail()
{
    handler.overrideSpeedLimit = true;
    mock.sendOk = false;

    CanFrame f = {.id = 1080};
    f.dlc = 8;
    f.data[7] = 0x80;
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacyFsdDiag.aux1080.err);
    TEST_ASSERT_EQUAL(FsdSkipReason::TxFail, handler.legacyFsdDiag.aux1080.lastSkip);
}

// --- No sends on unrelated CAN IDs ---

void test_legacy_ignores_unrelated_can_id()
{
    CanFrame f = {.id = 999};
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// --- Filter IDs ---

void test_legacy_filter_ids_count()
{
    TEST_ASSERT_EQUAL_UINT8(10, handler.filterIdCount());
}

void test_legacy_filter_ids_values()
{
    const uint32_t *ids = handler.filterIds();
    TEST_ASSERT_EQUAL_UINT32(69, ids[0]);
    TEST_ASSERT_EQUAL_UINT32(280, ids[1]);
    TEST_ASSERT_EQUAL_UINT32(390, ids[2]);
    TEST_ASSERT_EQUAL_UINT32(760, ids[3]);
    TEST_ASSERT_EQUAL_UINT32(880, ids[4]); // 0x370 EPAS3P_sysStatus (EPAS-faithful nag)
    TEST_ASSERT_EQUAL_UINT32(920, ids[5]);
    TEST_ASSERT_EQUAL_UINT32(921, ids[6]);
    TEST_ASSERT_EQUAL_UINT32(1006, ids[7]);
    TEST_ASSERT_EQUAL_UINT32(1080, ids[8]);
    TEST_ASSERT_EQUAL_UINT32(CAN_ID_OTA_STATUS, ids[9]);
}

void test_legacy_health_no_source_when_mux0_stale()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 1000;
    FsdHealthState s = classifyLegacyFsdHealth(d, 7000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::NoSourceFrame, s);
}

void test_legacy_health_tx_degraded_recovers_after_success()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 6500;
    d.mux0.err = 1;
    d.mux0.lastSkip = FsdSkipReason::TxFail;
    d.mux0.lastTxMs = 6600;

    FsdHealthState degraded = classifyLegacyFsdHealth(d, 7000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::TxDegraded, degraded);

    d.mux0.tx = 1;
    d.mux0.lastSkip = FsdSkipReason::Sent;
    d.mux0.lastTxMs = 6800;
    d.health = FsdHealthState::Healthy;

    FsdHealthState recovered = classifyLegacyFsdHealth(d, 7000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::Healthy, recovered);
}

void test_legacy_health_recovers_when_mux0_success_after_aux_fail()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 8000;
    d.aux760.lastSkip = FsdSkipReason::TxFail;
    d.aux760.lastTxMs = 8500;
    d.aux760.err = 1;

    FsdHealthState degraded = classifyLegacyFsdHealth(d, 9000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::TxDegraded, degraded);

    d.mux0.lastSkip = FsdSkipReason::Sent;
    d.mux0.lastTxMs = 8900;
    d.mux0.tx = 1;
    d.health = FsdHealthState::Healthy;

    FsdHealthState recovered = classifyLegacyFsdHealth(d, 9000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::Healthy, recovered);
}

void test_legacy_health_same_ms_success_then_fail_is_degraded()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 9000;
    d.mux0.tx = 1;
    d.mux0.lastSkip = FsdSkipReason::Sent;
    d.mux0.lastTxMs = 9000;
    d.mux0.lastTxSeq = 1;
    d.mux1.err = 1;
    d.mux1.lastSkip = FsdSkipReason::TxFail;
    d.mux1.lastTxMs = 9000;
    d.mux1.lastTxSeq = 2;
    d.health = FsdHealthState::TxDegraded;

    TEST_ASSERT_EQUAL(FsdHealthState::TxDegraded, classifyLegacyFsdHealth(d, 9001, true));
}

void test_legacy_health_same_ms_fail_then_success_is_healthy()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 9000;
    d.mux0.tx = 1;
    d.mux0.lastSkip = FsdSkipReason::Sent;
    d.mux0.lastTxMs = 9000;
    d.mux0.lastTxSeq = 2;
    d.mux1.err = 1;
    d.mux1.lastSkip = FsdSkipReason::TxFail;
    d.mux1.lastTxMs = 9000;
    d.mux1.lastTxSeq = 1;
    d.health = FsdHealthState::Healthy;

    TEST_ASSERT_EQUAL(FsdHealthState::Healthy, classifyLegacyFsdHealth(d, 9001, true));
}

void test_legacy_health_gate_blocked_when_last_blocked()
{
    LegacyFsdDiag d;
    d.mux0.lastRxMs = 6500;
    d.health = FsdHealthState::GateBlocked;
    FsdHealthState s = classifyLegacyFsdHealth(d, 7000, true);
    TEST_ASSERT_EQUAL(FsdHealthState::GateBlocked, s);
}

// ============================================================
// TSL6P Mode B v4 — opt-in 0x370 echo burst, LegacyHandler only.
// ============================================================

void test_legacy_tsl6p_off_no_echo()
{
    handler.bionicSteering = false;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);

    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_legacy_tsl6p_hos3_sends_first_sequence_frame()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);

    CanFrame epas = makeEpasFrame(0, 0.20f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(880, mock.sent[0].id);
    TEST_ASSERT_EQUAL_INT(180, decodeEchoTorqueSigned(mock.sent[0]));
    TEST_ASSERT_EQUAL_UINT8(1, decodeEchoHandsOnLevel(mock.sent[0]));
    TEST_ASSERT_EQUAL_UINT8(3, counterLowNibble(mock.sent[0]));
    TEST_ASSERT_TRUE(checksumValid370(mock.sent[0]));
}

void test_legacy_tsl6p_sequence_cycles_absolute_torque_targets()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);

    const int expected[] = {180, 150, -150, -180, 180};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        CanFrame epas = makeEpasFrame(0, 0.20f, static_cast<uint8_t>(i));
        handler.handleMessage(epas, mock);
        TEST_ASSERT_EQUAL_INT(expected[i], decodeEchoTorqueSigned(mock.sent[i]));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>((i + 1) & 0x0F), counterLowNibble(mock.sent[i]));
        TEST_ASSERT_TRUE(checksumValid370(mock.sent[i]));
    }
}

void test_legacy_tsl6p_burst_off_suppresses_echo()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    handler.nag.onNagSample(3, 1100, true, 6);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, handler.nag.mode());

    CanFrame epasNext = makeEpasFrame(0, 0.10f, 3);
    handler.handleMessage(epasNext, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
}


void test_legacy_tsl6p_370_path_advances_cycle_without_fresh_399()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, handler.nag.mode());

    for (int i = 0; i < 1200; ++i)
    {
        CanFrame epas = makeEpasFrame(0, 0.10f, static_cast<uint8_t>(i));
        handler.handleMessage(epas, mock);
    }
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, handler.nag.mode());
    size_t sentBeforeOffExpires = mock.sent.size();

    for (int i = 0; i < 1700; ++i)
    {
        CanFrame epas = makeEpasFrame(0, 0.10f, static_cast<uint8_t>(i));
        handler.handleMessage(epas, mock);
    }

    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, handler.nag.mode());
    TEST_ASSERT_TRUE(mock.sent.size() > sentBeforeOffExpires);
}

void test_legacy_tsl6p_hos_clear_stops_future_echo()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    CanFrame dasClear = makeDasFrameWithState(2, 6);
    handler.handleMessage(dasClear, mock);
    CanFrame epasNext = makeEpasFrame(0, 0.10f, 3);
    handler.handleMessage(epasNext, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, handler.nag.mode());
}

void test_legacy_tsl6p_checkad_blocks_and_cancels()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, handler.nag.mode());

    handler.checkAD = denyAD;
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, handler.nag.mode());
    TEST_ASSERT_EQUAL_STRING("checkAD", handler.nag.blockedReason());
}

void test_legacy_tsl6p_checkad_false_from_idle_does_not_start_burst_session()
{
    handler.bionicSteering = true;
    handler.checkAD = denyAD;

    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, handler.nag.mode());
    TEST_ASSERT_EQUAL_UINT32(0, handler.nag.burstSessions());
    TEST_ASSERT_EQUAL_STRING("checkAD", handler.nag.blockedReason());
}

void test_legacy_tsl6p_burst_off_gate_loss_cancels_with_reason()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    handler.nag.onNagSample(3, 5000, true, 6);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_OFF, handler.nag.mode());

    handler.bionicSteering = false;
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, handler.nag.mode());
    TEST_ASSERT_EQUAL_STRING("toggle", handler.nag.blockedReason());
}

void test_legacy_tsl6p_ap_inactive_cancels()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, handler.nag.mode());

    CanFrame dasInactive = makeDasFrameWithState(3, 2);
    handler.handleMessage(dasInactive, mock);
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL(HumanReplayMode::IDLE, handler.nag.mode());
    TEST_ASSERT_EQUAL_STRING("apInactive", handler.nag.blockedReason());
}

void test_legacy_tsl6p_abort_state_blocks_subsequent_echo()
{
    handler.bionicSteering = true;
    CanFrame das = makeDasFrameWithState(3, 6);
    handler.handleMessage(das, mock);
    TEST_ASSERT_EQUAL(HumanReplayMode::BURST_ON, handler.nag.mode());

    CanFrame dasAbort = makeDasFrameWithState(3, 8);
    handler.handleMessage(dasAbort, mock);
    CanFrame epas = makeEpasFrame(0, 0.10f, 2);
    handler.handleMessage(epas, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL(HumanReplayMode::COOLDOWN, handler.nag.mode());
    TEST_ASSERT_EQUAL_STRING("abort", handler.nag.blockedReason());
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_legacy_filter_ids_count);
    RUN_TEST(test_legacy_filter_ids_values);
    RUN_TEST(test_legacy_health_no_source_when_mux0_stale);
    RUN_TEST(test_legacy_health_tx_degraded_recovers_after_success);
    RUN_TEST(test_legacy_health_recovers_when_mux0_success_after_aux_fail);
    RUN_TEST(test_legacy_health_same_ms_success_then_fail_is_degraded);
    RUN_TEST(test_legacy_health_same_ms_fail_then_success_is_healthy);
    RUN_TEST(test_legacy_health_gate_blocked_when_last_blocked);

    RUN_TEST(test_legacy_stalk_pos0_sets_profile_2);
    RUN_TEST(test_legacy_stalk_pos1_sets_profile_2);
    RUN_TEST(test_legacy_stalk_pos2_sets_profile_1);
    RUN_TEST(test_legacy_stalk_pos3_sets_profile_0);
    RUN_TEST(test_legacy_manual_profile_ignores_stalk_position);

    RUN_TEST(test_legacy_AD_enabled_on_mux0);
    RUN_TEST(test_legacy_no_send_when_AD_disabled);
    RUN_TEST(test_legacy_AD_sets_bit46);
    RUN_TEST(test_legacy_stable_mux0_sets_bit46_only);
    RUN_TEST(test_legacy_checkAD_blocks_mux0_send);
    RUN_TEST(test_legacy_fsdTriggered_set_on_mux0);

    RUN_TEST(test_legacy_stable_mux1_is_disabled);
    RUN_TEST(test_legacy_experimental_mux0_can_write_speed_profile);
    RUN_TEST(test_legacy_experimental_mux1_can_clear_bit19);
    RUN_TEST(test_legacy_tesla_parity_mux0_writes_speed_profile);
    RUN_TEST(test_legacy_tesla_parity_mux1_clears_bit19);
    RUN_TEST(test_legacy_vision_clear_works_under_tesla_parity_policy);
    RUN_TEST(test_legacy_vision_clear_disabled_does_not_clear_bit48);
    RUN_TEST(test_legacy_trigger_source_force_vs_ui_bit_vs_false);
    RUN_TEST(test_legacy_mux0_records_can_a_twai_labels);
    RUN_TEST(test_legacy_gate_block_records_gate_blocked_health);
    RUN_TEST(test_legacy_gate_block_uses_resolver_reason);

    RUN_TEST(test_legacy_can760_writes_offset);
    RUN_TEST(test_legacy_can760_skips_when_offset_zero);
    RUN_TEST(test_legacy_can921_captures_fused_speed_limit);
    RUN_TEST(test_legacy_can760_fixed_pct_writes_simple_offset);
    RUN_TEST(test_legacy_can760_auto_writes_simple_offset);
    RUN_TEST(test_legacy_can760_custom_writes_simple_offset);
    RUN_TEST(test_legacy_can760_clamps_simple_offset_to_wire_max);
    RUN_TEST(test_legacy_can760_checkAD_blocks_computed_offset);
    RUN_TEST(test_legacy_can760_records_tx_fail);
    RUN_TEST(test_legacy_can1080_sets_vision_slider);
    RUN_TEST(test_legacy_can1080_skips_when_disabled);
    RUN_TEST(test_legacy_can1080_records_tx_fail);

    RUN_TEST(test_legacy_ignores_unrelated_can_id);

    RUN_TEST(test_legacy_tsl6p_off_no_echo);
    RUN_TEST(test_legacy_tsl6p_hos3_sends_first_sequence_frame);
    RUN_TEST(test_legacy_tsl6p_sequence_cycles_absolute_torque_targets);
    RUN_TEST(test_legacy_tsl6p_burst_off_suppresses_echo);
    RUN_TEST(test_legacy_tsl6p_370_path_advances_cycle_without_fresh_399);
    RUN_TEST(test_legacy_tsl6p_hos_clear_stops_future_echo);
    RUN_TEST(test_legacy_tsl6p_checkad_blocks_and_cancels);
    RUN_TEST(test_legacy_tsl6p_checkad_false_from_idle_does_not_start_burst_session);
    RUN_TEST(test_legacy_tsl6p_burst_off_gate_loss_cancels_with_reason);
    RUN_TEST(test_legacy_tsl6p_ap_inactive_cancels);
    RUN_TEST(test_legacy_tsl6p_abort_state_blocks_subsequent_echo);

    return UNITY_END();
}
