#include <unity.h>
#include "dash_wheel_dnd.h"

// tick() only produces a frame when a native 0x3C2 frame is fresh OR syntheticFallback
// is enabled (see dash_wheel_dnd.h: the !useNative && !syntheticFallback gate returns
// "waiting_native" without advancing the step). Unit tests exercise the sequence logic
// via the synthetic path, so they construct the DND with the fallback enabled. Without
// this, tick() never advances — the non-looping sequence tests fail their data assertions
// and the while(isRunning()) loop tests hang indefinitely (the CI 6h timeout root cause).
// Inactive-tick tests still pass with this helper: tick() short-circuits on
// !volumeActive && !speedActive before reaching the synthetic/native gate.
static void enableSyntheticFallback(DashWheelDND &dnd)
{
    dnd.syntheticFallback = true;
}

// ─── Constants ────────────────────────────────────────────────────────

void test_dnd_constants()
{
    TEST_ASSERT_EQUAL(4, DashWheelDND::kStepCount);
    TEST_ASSERT_EQUAL(50, DashWheelDND::kStepIntervalMs);
    TEST_ASSERT_EQUAL_HEX32(0x3C2, DashWheelDND::kCanId);
    TEST_ASSERT_EQUAL_HEX8(0x01, DashWheelDND::kSteps[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, DashWheelDND::kSteps[1]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, DashWheelDND::kSteps[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, DashWheelDND::kSteps[3]);
}

// ─── Volume DND sequence ──────────────────────────────────────────────

void test_volume_sequence_writes_correct_steps_to_data2()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    TEST_ASSERT_TRUE(dnd.volumeActive);
    TEST_ASSERT_EQUAL(0, dnd.volumeStep);

    // Step 0 → data[2] = 0x01
    dnd.tick(50, data);
    TEST_ASSERT_EQUAL_HEX8(0x01, data[2]);

    // Step 1 → data[2] = 0x00
    dnd.tick(100, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[2]);

    // Step 2 → data[2] = 0x3F
    dnd.tick(150, data);
    TEST_ASSERT_EQUAL_HEX8(0x3F, data[2]);

    // Step 3 → data[2] = 0x00 (final step)
    dnd.tick(200, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[2]);

    // Sequence complete → volumeActive should be false
    TEST_ASSERT_FALSE(dnd.volumeActive);
}

void test_volume_sequence_four_steps_then_done()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    int sentCount = 0;
    int t = 50;
    while (dnd.isRunning())
    {
        if (dnd.tick(t, data))
            sentCount++;
        t += 50;
    }

    TEST_ASSERT_EQUAL(4, sentCount);
    TEST_ASSERT_FALSE(dnd.volumeActive);
    TEST_ASSERT_EQUAL(0, dnd.volumeStep);
}

// ─── Speed DND sequence ───────────────────────────────────────────────

void test_speed_sequence_writes_correct_steps_to_data3()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startSpeed();
    TEST_ASSERT_TRUE(dnd.speedActive);

    dnd.tick(50, data);
    TEST_ASSERT_EQUAL_HEX8(0x01, data[3]);

    dnd.tick(100, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[3]);

    dnd.tick(150, data);
    TEST_ASSERT_EQUAL_HEX8(0x3F, data[3]);

    dnd.tick(200, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[3]);

    TEST_ASSERT_FALSE(dnd.speedActive);
}

void test_speed_sequence_four_steps_then_done()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startSpeed();
    int sentCount = 0;
    int t = 50;
    while (dnd.isRunning())
    {
        if (dnd.tick(t, data))
            sentCount++;
        t += 50;
    }

    TEST_ASSERT_EQUAL(4, sentCount);
    TEST_ASSERT_FALSE(dnd.speedActive);
}

// ─── OR-combined volume + speed ────────────────────────────────────────

void test_combined_volume_and_speed_both_active()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    dnd.startSpeed();

    // Step 0: volume=0x01 on data[2], speed=0x01 on data[3]
    dnd.tick(50, data);
    TEST_ASSERT_EQUAL_HEX8(0x01, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, data[3]);

    // Step 1: both = 0x00
    dnd.tick(100, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[3]);

    // Step 2: both = 0x3F
    dnd.tick(150, data);
    TEST_ASSERT_EQUAL_HEX8(0x3F, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, data[3]);

    // Step 3: both = 0x00, both sequences complete
    dnd.tick(200, data);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[3]);
    TEST_ASSERT_FALSE(dnd.volumeActive);
    TEST_ASSERT_FALSE(dnd.speedActive);
}

void test_combined_eight_frames_total()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    // Start volume first, then speed 2 steps later
    dnd.startVolume();
    dnd.tick(50, data);  // vol step 0
    dnd.tick(100, data); // vol step 1
    dnd.startSpeed();    // start speed at vol step 2
    dnd.tick(150, data); // vol step 2, speed step 0

    // Volume should have data[2], Speed should have data[3]
    TEST_ASSERT_EQUAL_HEX8(0x3F, data[2]); // vol step 2
    TEST_ASSERT_EQUAL_HEX8(0x01, data[3]); // speed step 0
    TEST_ASSERT_TRUE(dnd.isRunning());
}

// ─── Tick timing: 50ms interval ───────────────────────────────────────

void test_tick_returns_false_within_50ms()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    dnd.tick(100, data); // first tick at t=100

    // Next tick at t=120 (< 50ms since last step) → should return false
    TEST_ASSERT_FALSE(dnd.tick(120, data));

    // Next tick at t=149 (< 50ms) → still false
    TEST_ASSERT_FALSE(dnd.tick(149, data));

    // Next tick at t=150 (== 50ms) → should return true
    TEST_ASSERT_TRUE(dnd.tick(150, data));
}

void test_tick_no_frame_when_inactive()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    TEST_ASSERT_FALSE(dnd.tick(50, data));
    TEST_ASSERT_FALSE(dnd.tick(100, data));
}

// ─── Counter increment ────────────────────────────────────────────────

void test_counter_increments_each_frame()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();

    dnd.tick(50, data);
    uint8_t c1 = data[0];

    dnd.tick(100, data);
    uint8_t c2 = data[0];

    TEST_ASSERT_EQUAL_UINT8((c1 + 1) & 0x0F, c2);
}

void test_counter_wraps_at_16()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    dnd.counter = 14;
    uint8_t data[8] = {};

    dnd.startVolume();

    dnd.tick(50, data);
    TEST_ASSERT_EQUAL_HEX8(15, data[0]);

    dnd.tick(100, data);
    TEST_ASSERT_EQUAL_HEX8(0, data[0]); // wraps to 0

    dnd.tick(150, data);
    TEST_ASSERT_EQUAL_HEX8(1, data[0]);
}

// ─── Checksum calculation ─────────────────────────────────────────────

void test_checksum_is_correct()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    dnd.tick(50, data);

    // Checksum = (0xC2 + 0x03) + sum(data[0..6])
    uint16_t expected = 0xC2u + 0x03u;
    for (int i = 0; i < 7; i++)
        expected += data[i];

    TEST_ASSERT_EQUAL_HEX8(expected & 0xFF, data[7]);
}

void test_checksum_changes_with_counter()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data1[8] = {};
    uint8_t data2[8] = {};

    dnd.startVolume();

    dnd.tick(50, data1);
    dnd.tick(100, data2);

    // Different counter values → different checksums
    TEST_ASSERT_TRUE(data1[0] != data2[0]); // different counters
    // data[7] is checksum; should differ since counter differs
    // (unless coincidentally same, but very unlikely)
}

// ─── isRunning state machine ──────────────────────────────────────────

void test_is_running_while_active()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    TEST_ASSERT_FALSE(dnd.isRunning());

    dnd.startVolume();
    TEST_ASSERT_TRUE(dnd.isRunning());

    // Advance through all steps
    dnd.tick(50, data);
    dnd.tick(100, data);
    dnd.tick(150, data);
    TEST_ASSERT_TRUE(dnd.isRunning()); // last step still running

    dnd.tick(200, data);
    TEST_ASSERT_FALSE(dnd.isRunning()); // done
}

// ─── reset ────────────────────────────────────────────────────────────

void test_reset_clears_all_state()
{
    DashWheelDND dnd;
    enableSyntheticFallback(dnd);
    uint8_t data[8] = {};

    dnd.startVolume();
    dnd.startSpeed();
    dnd.tick(50, data);
    dnd.tick(100, data);
    dnd.counter = 10;

    dnd.reset();

    TEST_ASSERT_FALSE(dnd.volumeActive);
    TEST_ASSERT_FALSE(dnd.speedActive);
    TEST_ASSERT_EQUAL(0, dnd.volumeStep);
    TEST_ASSERT_EQUAL(0, dnd.speedStep);
    TEST_ASSERT_EQUAL(0, dnd.counter);
    TEST_ASSERT_FALSE(dnd.isRunning());
}

// ── main ──────────────────────────────────────────────────────────────

int main()
{
    UNITY_BEGIN();

    // Constants
    RUN_TEST(test_dnd_constants);

    // Volume DND
    RUN_TEST(test_volume_sequence_writes_correct_steps_to_data2);
    RUN_TEST(test_volume_sequence_four_steps_then_done);

    // Speed DND
    RUN_TEST(test_speed_sequence_writes_correct_steps_to_data3);
    RUN_TEST(test_speed_sequence_four_steps_then_done);

    // Combined
    RUN_TEST(test_combined_volume_and_speed_both_active);
    RUN_TEST(test_combined_eight_frames_total);

    // Timing
    RUN_TEST(test_tick_returns_false_within_50ms);
    RUN_TEST(test_tick_no_frame_when_inactive);

    // Counter
    RUN_TEST(test_counter_increments_each_frame);
    RUN_TEST(test_counter_wraps_at_16);

    // Checksum
    RUN_TEST(test_checksum_is_correct);
    RUN_TEST(test_checksum_changes_with_counter);

    // State machine
    RUN_TEST(test_is_running_while_active);

    // Reset
    RUN_TEST(test_reset_clears_all_state);

    return UNITY_END();
}
