#include <unity.h>

#include "dash_abort_guard.h"

// (The DashMinimalInject fixture and its three tests were removed in v1.18
// with the #108 steer-jerk defense family; Abort Guard itself is kept — it
// is the 0x399 state 8/9 latch safety guard.)
static DashAbortGuard guard;

void setUp()
{
    guard = DashAbortGuard{};
}

void tearDown() {}

void test_default_disabled_allows_injection_even_on_abort_state()
{
    guard.onApState(8, 1000);
    TEST_ASSERT_FALSE(guard.diag().enabled);
    TEST_ASSERT_FALSE(guard.diag().latched);
    TEST_ASSERT_TRUE(guard.allowsInjection());
}

void test_enabled_state6_does_not_latch()
{
    guard.setEnabled(true);
    guard.onApState(6, 1000);
    TEST_ASSERT_TRUE(guard.diag().enabled);
    TEST_ASSERT_FALSE(guard.diag().latched);
    TEST_ASSERT_TRUE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_UINT8(6, guard.diag().lastApState);
}

void test_state8_latches_and_blocks()
{
    guard.setEnabled(true);
    guard.onApState(8, 1000);
    TEST_ASSERT_TRUE(guard.diag().latched);
    TEST_ASSERT_FALSE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_UINT8(8, guard.diag().lastAbortState);
    TEST_ASSERT_EQUAL_UINT32(1000, guard.diag().latchedAtMs);
}

void test_state9_latches_and_blocks()
{
    guard.setEnabled(true);
    guard.onApState(9, 2000);
    TEST_ASSERT_TRUE(guard.diag().latched);
    TEST_ASSERT_FALSE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_UINT8(9, guard.diag().lastAbortState);
}

void test_latched_state6_does_not_clear()
{
    guard.setEnabled(true);
    guard.onApState(8, 1000);
    guard.onApState(6, 1100);
    TEST_ASSERT_TRUE(guard.diag().latched);
    TEST_ASSERT_FALSE(guard.allowsInjection());
}

void test_latched_state1_clears()
{
    guard.setEnabled(true);
    guard.onApState(9, 1000);
    guard.onApState(1, 1500);
    TEST_ASSERT_FALSE(guard.diag().latched);
    TEST_ASSERT_TRUE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_STRING("cleanDisengage", guard.diag().lastClearReason);
}

void test_latched_state2_does_not_clear()
{
    // Upstream v2.16-beta.11: state 2 (ACTIVE_NOMINAL) is NOT a clean disengage
    // (clean = das_ap_state < 2), so the latch stays armed and injection stays
    // suppressed until a true UNAVAIL/AVAIL (< 2) state arrives.
    guard.setEnabled(true);
    guard.onApState(8, 1000);
    guard.onApState(2, 1500);
    TEST_ASSERT_TRUE(guard.diag().latched);
    TEST_ASSERT_FALSE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_UINT8(2, guard.diag().lastApState);
}

// (test_minimal_inject_is_default_off_and_unbounded,
// test_minimal_inject_allows_exactly_five_per_engagement, and
// test_minimal_inject_resets_only_after_non_engaged_state were removed in
// v1.18 with the DashMinimalInject class — see the note above.)

void test_record_block_counts_path_only_when_blocked()
{
    guard.setEnabled(true);
    guard.recordBlock(DashAbortGuardBlockPath::LegacyFsdMux0);
    TEST_ASSERT_EQUAL_UINT32(0, guard.diag().blocks);
    TEST_ASSERT_EQUAL_STRING("none", guard.diag().lastBlockedPath);

    guard.onApState(8, 1000);
    guard.recordBlock(DashAbortGuardBlockPath::LegacyFsdMux0);
    guard.recordBlock(DashAbortGuardBlockPath::LegacySpeed0x2f8);
    TEST_ASSERT_EQUAL_UINT32(2, guard.diag().blocks);
    TEST_ASSERT_EQUAL_STRING("legacy_speed_0x2f8", guard.diag().lastBlockedPath);
}

void test_disabling_guard_clears_latch()
{
    guard.setEnabled(true);
    guard.onApState(8, 1000);
    guard.setEnabled(false);
    TEST_ASSERT_FALSE(guard.diag().enabled);
    TEST_ASSERT_FALSE(guard.diag().latched);
    TEST_ASSERT_TRUE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_STRING("disabled", guard.diag().lastClearReason);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_disabled_allows_injection_even_on_abort_state);
    RUN_TEST(test_enabled_state6_does_not_latch);
    RUN_TEST(test_state8_latches_and_blocks);
    RUN_TEST(test_state9_latches_and_blocks);
    RUN_TEST(test_latched_state6_does_not_clear);
    RUN_TEST(test_latched_state1_clears);
    RUN_TEST(test_latched_state2_does_not_clear);
    RUN_TEST(test_record_block_counts_path_only_when_blocked);
    RUN_TEST(test_disabling_guard_clears_latch);
    return UNITY_END();
}
