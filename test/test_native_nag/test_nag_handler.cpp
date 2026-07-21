#include <unity.h>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "dash_nag_runtime.h"
#include "drivers/mock_driver.h"
#include <cstring>
#include "real_epas_frames.h"

// Build a CanFrame (id 880 / dlc 8) from a raw-bytes sample. Stored as raw
// bytes in the fixture to keep the generator portable (no designated-init of
// the data[] array in the header).
static CanFrame makeFrameFromSample(const RealEpasSample &s)
{
    CanFrame f;
    f.id = 880;
    f.dlc = 8;
    std::memcpy(f.data, s.bytes, 8);
    return f;
}

static MockDriver mock;
static NagHandler handler;

// Helper: build a realistic CAN 880 frame
static CanFrame makeEpasFrame(uint8_t handsOn, float torqueNm, uint8_t counter, uint8_t eacStatus = 2)
{
    CanFrame f = {.id = 880, .dlc = 8};
    // bytes 0-1: steeringRackForce (arbitrary realistic values)
    f.data[0] = 0x12;
    f.data[1] = 0x00;
    // bytes 2-3: torsionBarTorque = (torque + 20.5) / 0.01
    uint16_t tRaw = static_cast<uint16_t>((torqueNm + 20.5) / 0.01);
    f.data[2] = 0x08 | ((tRaw >> 8) & 0x0F); // upper nibble = flags (0x08)
    f.data[3] = tRaw & 0xFF;
    // byte 4: handsOnLevel in bits 7:6, internalSAS bits in lower
    f.data[4] = static_cast<uint8_t>((handsOn & 0x03) << 6) | 0x1F;
    // byte 5: internalSAS LSB
    f.data[5] = 0x89;
    // byte 6: upper nibble = eacStatus/tireID, lower nibble = counter
    f.data[6] = static_cast<uint8_t>((eacStatus << 5) | (counter & 0x0F));
    // byte 7: checksum = sum(b0..b6) + 0x73
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return f;
}

static CanFrame makeApStateFrame(uint8_t apState, uint8_t handsOnState)
{
    CanFrame f = {.id = NagHandler::kApStateId, .dlc = 8};
    f.data[0] = static_cast<uint8_t>(apState & 0x0F);
    f.data[5] = static_cast<uint8_t>((handsOnState & 0x0F) << 2);
    return f;
}

static CanFrame makeSteeringFrame(float degrees)
{
    CanFrame f = {.id = NagHandler::kSteeringId, .dlc = 8};
    uint16_t raw = static_cast<uint16_t>((degrees + 819.2f) * 10.0f + 0.5f);
    f.data[2] = static_cast<uint8_t>(raw & 0xFF);
    f.data[3] = static_cast<uint8_t>(0x40 | ((raw >> 8) & 0x3F));
    return f;
}

// Helper: verify checksum of a frame
static bool verifyChecksum(const CanFrame &f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    return f.data[7] == static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static bool containsId(const uint32_t *ids, uint8_t count, uint32_t id)
{
    for (uint8_t i = 0; i < count; i++)
    {
        if (ids[i] == id)
            return true;
    }
    return false;
}

void test_nag_runtime_filter_merge_mode_a_deduplicates_target()
{
    const uint32_t handlerIds[] = {69, 880, 921};
    const uint32_t nagIds[] = {880};
    uint32_t merged[8] = {};
    uint8_t count = dashMergeNagFilterIds(handlerIds, 3, nagIds, 1, merged, 8);
    TEST_ASSERT_EQUAL_UINT8(3, count);
    TEST_ASSERT_TRUE(containsId(merged, count, 880));
}

void test_nag_runtime_filter_merge_mode_c_includes_all_context()
{
    const uint32_t handlerIds[] = {69, 880, 921};
    const uint32_t nagIds[] = {880, 0x399, 0x129};
    uint32_t merged[8] = {};
    uint8_t count = dashMergeNagFilterIds(handlerIds, 3, nagIds, 3, merged, 8);
    // 0x399 is decimal 921 and is already present in the handler list.
    TEST_ASSERT_EQUAL_UINT8(4, count);
    TEST_ASSERT_TRUE(containsId(merged, count, 880));
    TEST_ASSERT_TRUE(containsId(merged, count, 0x399));
    TEST_ASSERT_TRUE(containsId(merged, count, 0x129));
}

void test_nag_route_reports_each_outer_gate()
{
    TEST_ASSERT_EQUAL(DashNagRouteBlock::ModeOff,
                      dashNagRouteBlock(false, true, true, true, true));
    TEST_ASSERT_EQUAL(DashNagRouteBlock::CanWriteOff,
                      dashNagRouteBlock(true, false, true, true, true));
    TEST_ASSERT_EQUAL(DashNagRouteBlock::Ota,
                      dashNagRouteBlock(true, true, false, true, true));
    TEST_ASSERT_EQUAL(DashNagRouteBlock::ApGate,
                      dashNagRouteBlock(true, true, true, false, true));
    TEST_ASSERT_EQUAL(DashNagRouteBlock::AbortGuard,
                      dashNagRouteBlock(true, true, true, true, false));
    TEST_ASSERT_EQUAL(DashNagRouteBlock::None,
                      dashNagRouteBlock(true, true, true, true, true));
}

void setUp()
{
    mock.reset();
    handler = NagHandler();
    handler.enablePrint = false;
}

void tearDown() {}

// ============================================================
// Filter IDs
// ============================================================

void test_nag_filter_ids_count()
{
    TEST_ASSERT_EQUAL_UINT8(1, handler.filterIdCount());
}

void test_nag_filter_ids_value()
{
    const uint32_t *ids = handler.filterIds();
    TEST_ASSERT_EQUAL_UINT32(880, ids[0]);
    ids = handler.modeFilterIds();
    TEST_ASSERT_EQUAL_UINT8(1, handler.modeFilterIdCount(static_cast<uint8_t>(BuiltInNagMode::ModeA)));
    TEST_ASSERT_EQUAL_UINT8(3, handler.modeFilterIdCount(static_cast<uint8_t>(BuiltInNagMode::ModeC)));
    TEST_ASSERT_EQUAL_UINT32(0x399, ids[1]);
    TEST_ASSERT_EQUAL_UINT32(0x129, ids[2]);
}

// ============================================================
// Basic echo behavior
// ============================================================

void test_nag_echoes_when_handson_0()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_nag_does_not_echo_when_handson_1()
{
    CanFrame f = makeEpasFrame(1, 1.5, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_does_not_echo_when_handson_2()
{
    CanFrame f = makeEpasFrame(2, 2.5, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_does_not_echo_when_handson_3()
{
    CanFrame f = makeEpasFrame(3, 3.0, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_does_not_echo_when_disabled()
{
    handler.nagKillerActive = false;
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_ignores_non_880_id()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.id = 881; // wrong ID
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_ignores_short_dlc()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.dlc = 7; // too short
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// ============================================================
// Counter+1 logic
// ============================================================

void test_nag_counter_increments_by_1()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t outCounter = mock.sent[0].data[6] & 0x0F;
    TEST_ASSERT_EQUAL_HEX8(0x0D, outCounter); // 0x0C + 1
}

void test_nag_counter_wraps_from_f_to_0()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0F);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t outCounter = mock.sent[0].data[6] & 0x0F;
    TEST_ASSERT_EQUAL_HEX8(0x00, outCounter); // 0x0F + 1 wraps to 0
}

void test_nag_counter_preserves_upper_nibble()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x05, 2); // eacStatus=2 -> upper nibble = 0x40
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t upperNibble = mock.sent[0].data[6] & 0xF0;
    uint8_t expectedUpper = f.data[6] & 0xF0;
    TEST_ASSERT_EQUAL_HEX8(expectedUpper, upperNibble);
}

// ============================================================
// Modified field values
// ============================================================

void test_nag_sets_handson_to_1()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    uint8_t outHandsOn = (mock.sent[0].data[4] >> 6) & 0x03;
    TEST_ASSERT_EQUAL_UINT8(1, outHandsOn);
}

void test_nag_preserves_byte4_lower_bits()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.data[4] = 0x1F; // handsOn=0, lower bits = 0x1F
    handler.handleMessage(f, mock);
    uint8_t outLower = mock.sent[0].data[4] & 0x3F;
    TEST_ASSERT_EQUAL_HEX8(0x1F, outLower); // lower 6 bits preserved
}

// ============================================================
// Upstream beta.8 Mode A fixed-torque encoding
// ============================================================

void test_nag_mode_a_sets_fixed_torque()
{
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xB6, mock.sent[0].data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x08, mock.sent[0].data[2] & 0x0F);
}

void test_nag_mode_a_torque_is_1_80_nm()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    uint16_t tRaw = ((mock.sent[0].data[2] & 0x0F) << 8) | mock.sent[0].data[3];
    float torque = tRaw * 0.01f - 20.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1.80, torque);
}

void test_nag_mode_a_preserves_non_torque_fields()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.data[0] = 0xAB;
    f.data[1] = 0xCD;
    f.data[2] = 0x8E; // upper nibble has flags
    f.data[3] = 0x77;
    f.data[5] = 0x42;
    // Recompute checksum after manual changes
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xAB, mock.sent[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, mock.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x88, mock.sent[0].data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xB6, mock.sent[0].data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x42, mock.sent[0].data[5]);
}

void test_nag_output_torque_never_exceeds_upstream_bounds()
{
    for (uint8_t cnt = 0; cnt < 16; cnt++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, -20.0 + cnt * 2.5, cnt);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());

        uint16_t tRaw = ((mock.sent[0].data[2] & 0x0F) << 8) | mock.sent[0].data[3];
        float torque = tRaw * 0.01f - 20.5f;

        // Must be exactly 1.80 Nm (from fixed byte 3 = 0xB6)
        TEST_ASSERT_FLOAT_WITHIN(0.1, 1.80, torque);
        // Must never exceed safe range
        TEST_ASSERT_TRUE(torque >= kNagTorqueNmMin);
        TEST_ASSERT_TRUE(torque <= kNagTorqueNmMax);
    }
}

// ============================================================
// Checksum verification
// ============================================================

void test_nag_checksum_correct()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_checksum_correct_at_counter_boundary()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0F); // counter wraps
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_checksum_correct_with_various_inputs()
{
    // Test across multiple counter values and torques
    for (uint8_t cnt = 0; cnt < 16; cnt++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, -5.0 + cnt * 0.7, cnt);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        TEST_ASSERT_TRUE_MESSAGE(verifyChecksum(mock.sent[0]), "Checksum failed for counter sweep");
    }
}

// ============================================================
// Canary: output handson level must stay at 1
// ============================================================

void test_nag_output_handson_never_exceeds_1()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    uint8_t ho = (mock.sent[0].data[4] >> 6) & 0x03;
    TEST_ASSERT_EQUAL_UINT8(1, ho); // exactly 1, never 2 or 3
}

// ============================================================
// Frame count tracking
// ============================================================

void test_nag_increments_frames_sent()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
}

void test_nag_increments_echo_count()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagEchoCount);
}

void test_nag_multiple_frames_count_correctly()
{
    for (int i = 0; i < 10; i++)
    {
        CanFrame f = makeEpasFrame(0, 0.33, i & 0x0F);
        handler.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL_UINT32(10, handler.nagEchoCount);
    TEST_ASSERT_EQUAL(10, mock.sent.size());
}

// ============================================================
// Edge case: mixed handsOn sequence
// ============================================================

void test_nag_echoes_only_handson_0_in_mixed_sequence()
{
    // Simulate: ho=0, ho=1, ho=0, ho=2, ho=0
    CanFrame f0a = makeEpasFrame(0, 0.33, 0x00);
    CanFrame f1 = makeEpasFrame(1, 1.50, 0x01);
    CanFrame f0b = makeEpasFrame(0, 0.10, 0x02);
    CanFrame f2 = makeEpasFrame(2, 2.50, 0x03);
    CanFrame f0c = makeEpasFrame(0, 0.05, 0x04);

    handler.handleMessage(f0a, mock);
    handler.handleMessage(f1, mock);
    handler.handleMessage(f0b, mock);
    handler.handleMessage(f2, mock);
    handler.handleMessage(f0c, mock);

    TEST_ASSERT_EQUAL(3, mock.sent.size()); // only 3 echoes for ho=0
}

// ============================================================
// Output ID is always 880
// ============================================================

void test_nag_output_id_is_880()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(880, mock.sent[0].id);
}

void test_nag_output_dlc_is_8()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT8(8, mock.sent[0].dlc);
}

// ============================================================
// Real-data bench validation (Jordan's captured 0x370 frames)
// ============================================================

// T1: every Mode-A echo emitted for a captured real frame is well formed.
void test_nag_real_frames_echo_wellformed()
{
    int echoed = 0;
    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        mock.reset();
        CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
        handler.handleMessage(f, mock);

        if (!kRealEpasSamples[i].expectEcho)
        {
            TEST_ASSERT_EQUAL(0, mock.sent.size());
            continue;
        }
        echoed++;
        const CanFrame &e = mock.sent[0];
        uint16_t sum = 0;
        for (int j = 0; j < 7; j++)
            sum += e.data[j];
        TEST_ASSERT_EQUAL_HEX8((sum + 0x73) & 0xFF, e.data[7]);
        uint8_t inCnt = f.data[6] & 0x0F;
        TEST_ASSERT_EQUAL_HEX8((inCnt + 1) & 0x0F, e.data[6] & 0x0F);
        TEST_ASSERT_EQUAL_UINT8(1, (e.data[4] >> 6) & 0x03);
        TEST_ASSERT_EQUAL_HEX8(0xB6, e.data[3]);
        TEST_ASSERT_EQUAL_HEX8(0x08, e.data[2] & 0x0F);
    }
    printf("[REAL-DATA] T1 mode=A echoed=%d\n", echoed);
}

// T2: total echo count over the real sequence == number of handsOn==0 frames.
void test_nag_real_echo_count_matches_handson0()
{
    size_t expected = 0;
    for (size_t i = 0; i < kRealEpasSampleCount; i++)
        if (kRealEpasSamples[i].expectEcho)
            expected++;

    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
        handler.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL_UINT32(expected, mock.sent.size());
}

// T3: counter-interleaving collision analysis (core).
// Simulate the bus timeline: real frames arrive in capture order; NagHandler
// emits an echo (counter = real+1) after each handsOn==0 frame. A "collision"
// is when a real frame's counter equals the immediately-preceding echo's
// counter -- i.e. EPAS would see a duplicate/stale counter (the 2026-06-19
// fault mechanism). We read the ACTUAL echo counter from NagHandler (not an
// assumption) to stay faithful to the implementation.
//
// NOTE: across warning-event boundaries the capture pauses, so the real
// counter sequence may show non-+2 deltas there. The distribution is printed
// so the finding is visible regardless of where gaps fall.
void test_nag_real_counter_interleave_analysis()
{
    int collisions = 0;
    int stridesPlus2 = 0;
    int stridesOther = 0;
    int prevRealCounter = -1;
    int prevEchoCounter = -1;

    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        CanFrame f = makeFrameFromSample(kRealEpasSamples[i]);
        uint8_t realCnt = f.data[6] & 0x0F;

        if (prevRealCounter >= 0)
        {
            int delta = (realCnt - prevRealCounter) & 0x0F;
            if (delta == 2)
                stridesPlus2++;
            else
                stridesOther++;
            // collision: this real frame duplicates the previous echo's counter
            if (prevEchoCounter >= 0 && realCnt == prevEchoCounter)
                collisions++;
        }
        prevRealCounter = realCnt;

        // drive NagHandler to obtain the actual echo counter
        int echoCounter = -1;
        if (kRealEpasSamples[i].expectEcho)
        {
            mock.reset();
            handler.handleMessage(f, mock);
            if (mock.sent.size() == 1)
                echoCounter = mock.sent[0].data[6] & 0x0F;
        }
        prevEchoCounter = echoCounter;
    }

    printf("[REAL-DATA] interleave: frames=%u strides(+2)=%d strides(other)=%d collisions=%d\n",
           (unsigned)kRealEpasSampleCount, stridesPlus2, stridesOther, collisions);

    // The analysis must have run over a real sequence.
    TEST_ASSERT_TRUE(stridesPlus2 + stridesOther > 0);
    // If the real sequence strides UNIFORMLY +2, echo(+1) cannot collide with
    // the next real frame (N -> N+2, echo is N+1). Assert the clean case.
    // If there are boundary gaps (stridesOther > 0), only print -- don't over-claim.
    if (stridesOther == 0 && stridesPlus2 > 0)
        TEST_ASSERT_EQUAL_INT(0, collisions);
}

// T4: handsOn signal sanity.
// NagHandler keys off 0x370 byte4 bits[7:6]. If these never go non-zero in the
// real capture -- even during the driver's manual dismissal -- then NagHandler's
// trigger is inert on this vehicle and cannot detect "hands returned". The
// authoritative nag level is 0x399 byte5 (per the public-source reference).
// This test SURFACES the finding; it does not hide it.
void test_nag_real_handson_signal_sanity()
{
    int handson0 = 0;
    int handsonNonzero = 0;
    for (size_t i = 0; i < kRealEpasSampleCount; i++)
    {
        uint8_t ho = (kRealEpasSamples[i].bytes[4] >> 6) & 0x03;
        if (ho == 0)
            handson0++;
        else
            handsonNonzero++;
    }
    printf("[REAL-DATA] handsOn: total=%u ==0=%d !=0=%d\n",
           (unsigned)kRealEpasSampleCount, handson0, handsonNonzero);

    // Sanity: every frame was classified into one bucket.
    TEST_ASSERT_EQUAL_INT((int)kRealEpasSampleCount, handson0 + handsonNonzero);
    // Expected finding (not asserted): handsonNonzero is small (~10/1256 in the
    // captured data) -- logged above for review, not hard-asserted.
}

void test_nag_disabled_mode_does_not_echo()
{
    handler.setMode(static_cast<uint8_t>(BuiltInNagMode::Disabled));
    CanFrame f = makeEpasFrame(0, 0.33, 1);
    handler.handleMessageAt(f, mock, 100);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_mode_b_cycles_torque_and_pauses()
{
    handler.setMode(static_cast<uint8_t>(BuiltInNagMode::ModeB));
    CanFrame f = makeEpasFrame(0, 0.33, 1);
    handler.handleMessageAt(f, mock, 1000);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX16(0x08B6, ((mock.sent[0].data[2] & 0x0F) << 8) | mock.sent[0].data[3]);

    f = makeEpasFrame(0, 0.33, 2);
    handler.handleMessageAt(f, mock, 1200);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX16(0x0898, ((mock.sent[1].data[2] & 0x0F) << 8) | mock.sent[1].data[3]);

    f = makeEpasFrame(0, 0.33, 3);
    handler.handleMessageAt(f, mock, 2000);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_EQUAL_STRING("modeBPause", handler.diag().blockedReason);
}

void test_nag_mode_c_blocks_without_context()
{
    handler.setMode(static_cast<uint8_t>(BuiltInNagMode::ModeC));
    CanFrame f = makeEpasFrame(0, 0.33, 1);
    handler.handleMessageAt(f, mock, 1000);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_STRING("no399", handler.diag().blockedReason);
}

void test_nag_mode_c_injects_after_state2_delay_with_fresh_context()
{
    handler.setMode(static_cast<uint8_t>(BuiltInNagMode::ModeC));
    CanFrame ap = makeApStateFrame(6, 2);
    CanFrame steering = makeSteeringFrame(2.0f);
    handler.handleMessageAt(ap, mock, 100);
    handler.handleMessageAt(steering, mock, 100);
    ap = makeApStateFrame(6, 2);
    steering = makeSteeringFrame(2.0f);
    handler.handleMessageAt(ap, mock, 2100);
    handler.handleMessageAt(steering, mock, 2100);
    // beta.12 parity: a real Mode-C level-1 frame remains eligible unless it
    // matches our own echo payload.
    CanFrame f = makeEpasFrame(1, 0.33, 1);
    handler.handleMessageAt(f, mock, 2200);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint16_t raw = static_cast<uint16_t>((mock.sent[0].data[2] & 0x0F) << 8) |
                   mock.sent[0].data[3];
    TEST_ASSERT_TRUE(raw >= kNagTorqueRawMin);
    TEST_ASSERT_TRUE(raw <= kNagTorqueRawMax);
    TEST_ASSERT_TRUE(raw < 0x0802);
    TEST_ASSERT_EQUAL_UINT8(1, (mock.sent[0].data[4] >> 6) & 0x03);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_mode_c_fails_closed_for_stale_or_large_angle_context()
{
    handler.setMode(static_cast<uint8_t>(BuiltInNagMode::ModeC));
    CanFrame ap = makeApStateFrame(6, 3);
    CanFrame steering = makeSteeringFrame(6.0f);
    handler.handleMessageAt(ap, mock, 100);
    handler.handleMessageAt(steering, mock, 100);
    ap = makeApStateFrame(6, 3);
    handler.handleMessageAt(ap, mock, 1200);
    handler.handleMessageAt(steering, mock, 1200);
    CanFrame f = makeEpasFrame(0, 0.33, 1);
    handler.handleMessageAt(f, mock, 2300);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_STRING("stale399", handler.diag().blockedReason);

    ap = makeApStateFrame(6, 3);
    steering = makeSteeringFrame(6.0f);
    handler.handleMessageAt(ap, mock, 2400);
    handler.handleMessageAt(steering, mock, 2400);
    f = makeEpasFrame(0, 0.33, 2);
    handler.handleMessageAt(f, mock, 2450);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_STRING("steeringAngle", handler.diag().blockedReason);
}

int main()
{
    UNITY_BEGIN();

    // Filter
    RUN_TEST(test_nag_filter_ids_count);
    RUN_TEST(test_nag_filter_ids_value);

    // Basic echo behavior
    RUN_TEST(test_nag_echoes_when_handson_0);
    RUN_TEST(test_nag_does_not_echo_when_handson_1);
    RUN_TEST(test_nag_does_not_echo_when_handson_2);
    RUN_TEST(test_nag_does_not_echo_when_handson_3);
    RUN_TEST(test_nag_does_not_echo_when_disabled);
    RUN_TEST(test_nag_ignores_non_880_id);
    RUN_TEST(test_nag_ignores_short_dlc);

    // Counter+1
    RUN_TEST(test_nag_counter_increments_by_1);
    RUN_TEST(test_nag_counter_wraps_from_f_to_0);
    RUN_TEST(test_nag_counter_preserves_upper_nibble);

    // Modified fields
    RUN_TEST(test_nag_sets_handson_to_1);
    RUN_TEST(test_nag_preserves_byte4_lower_bits);
    RUN_TEST(test_nag_mode_a_sets_fixed_torque);
    RUN_TEST(test_nag_mode_a_preserves_non_torque_fields);
    RUN_TEST(test_nag_mode_a_torque_is_1_80_nm);

    // Checksum
    RUN_TEST(test_nag_checksum_correct);
    RUN_TEST(test_nag_checksum_correct_at_counter_boundary);
    RUN_TEST(test_nag_checksum_correct_with_various_inputs);

    // Safety canary
    RUN_TEST(test_nag_output_torque_never_exceeds_upstream_bounds);
    RUN_TEST(test_nag_output_handson_never_exceeds_1);

    // Counters
    RUN_TEST(test_nag_increments_frames_sent);
    RUN_TEST(test_nag_increments_echo_count);
    RUN_TEST(test_nag_multiple_frames_count_correctly);

    // Edge cases
    RUN_TEST(test_nag_echoes_only_handson_0_in_mixed_sequence);

    // Output frame
    RUN_TEST(test_nag_output_id_is_880);
    RUN_TEST(test_nag_output_dlc_is_8);

    // Real-data bench validation
    RUN_TEST(test_nag_real_frames_echo_wellformed);
    RUN_TEST(test_nag_real_echo_count_matches_handson0);
    RUN_TEST(test_nag_real_counter_interleave_analysis);
    RUN_TEST(test_nag_real_handson_signal_sanity);

    // Upstream beta.8 modes and fail-closed context gates
    RUN_TEST(test_nag_disabled_mode_does_not_echo);
    RUN_TEST(test_nag_mode_b_cycles_torque_and_pauses);
    RUN_TEST(test_nag_mode_c_blocks_without_context);
    RUN_TEST(test_nag_mode_c_injects_after_state2_delay_with_fresh_context);
    RUN_TEST(test_nag_mode_c_fails_closed_for_stale_or_large_angle_context);
    RUN_TEST(test_nag_runtime_filter_merge_mode_a_deduplicates_target);
    RUN_TEST(test_nag_runtime_filter_merge_mode_c_includes_all_context);
    RUN_TEST(test_nag_route_reports_each_outer_gate);

    return UNITY_END();
}
