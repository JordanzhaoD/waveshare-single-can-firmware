#include <unity.h>

#include "dash_abort_guard.h"

static DashAbortGuard guard;
static DashMinimalInject minimal;

void setUp()
{
    guard = DashAbortGuard{};
    minimal = DashMinimalInject{};
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

void test_latched_state2_clears_as_available()
{
    guard.setEnabled(true);
    guard.onApState(8, 1000);
    guard.onApState(2, 1500);
    TEST_ASSERT_FALSE(guard.diag().latched);
    TEST_ASSERT_TRUE(guard.allowsInjection());
    TEST_ASSERT_EQUAL_STRING("cleanDisengage", guard.diag().lastClearReason);
}

void test_minimal_inject_is_default_off_and_unbounded()
{
    for (uint8_t i = 0; i < kDashMinimalInjectBudget + 2; ++i)
    {
        TEST_ASSERT_TRUE(minimal.allowsInjection());
        TEST_ASSERT_TRUE(minimal.recordInjection());
    }
    TEST_ASSERT_FALSE(minimal.diag().enabled);
    TEST_ASSERT_EQUAL_UINT8(0, minimal.diag().used);
}

void test_minimal_inject_allows_exactly_five_per_engagement()
{
    minimal.setEnabled(true);
    minimal.onApState(3);
    for (uint8_t i = 0; i < kDashMinimalInjectBudget; ++i)
    {
        TEST_ASSERT_TRUE(minimal.allowsInjection());
        TEST_ASSERT_TRUE(minimal.recordInjection());
    }
    TEST_ASSERT_FALSE(minimal.allowsInjection());
    TEST_ASSERT_FALSE(minimal.recordInjection());
    minimal.recordBlock("legacy_fsd_mux0");
    TEST_ASSERT_EQUAL_UINT8(kDashMinimalInjectBudget, minimal.diag().used);
    TEST_ASSERT_EQUAL_UINT32(1, minimal.diag().blocks);
    TEST_ASSERT_EQUAL_STRING("legacy_fsd_mux0", minimal.diag().lastBlockedPath);
}

void test_minimal_inject_resets_only_after_non_engaged_state()
{
    minimal.setEnabled(true);
    minimal.onApState(3);
    TEST_ASSERT_TRUE(minimal.recordInjection());
    minimal.onApState(6);
    TEST_ASSERT_EQUAL_UINT8(1, minimal.diag().used);
    minimal.onApState(8);
    TEST_ASSERT_EQUAL_UINT8(0, minimal.diag().used);
    TEST_ASSERT_EQUAL_STRING("disengage", minimal.diag().lastResetReason);
    TEST_ASSERT_FALSE(minimal.diag().apEngaged);
}

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
    RUN_TEST(test_latched_state2_clears_as_available);
    RUN_TEST(test_record_block_counts_path_only_when_blocked);
    RUN_TEST(test_disabling_guard_clears_latch);
    RUN_TEST(test_minimal_inject_is_default_off_and_unbounded);
    RUN_TEST(test_minimal_inject_allows_exactly_five_per_engagement);
    RUN_TEST(test_minimal_inject_resets_only_after_non_engaged_state);
    return UNITY_END();
}
