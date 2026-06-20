#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;

void setUp()
{
    mock.reset();
}

void tearDown() {}

void test_bypass_tlssc_build_flag_no_longer_forces_ui_bit_clear()
{
    CanFrame f = {};
    f.data[4] = 0x00;
    TEST_ASSERT_FALSE(isFSDSelectedInUI(f));
}

void test_ui_bit6_selects_ad()
{
    CanFrame f = {};
    f.data[4] = 0x40; // bit 6 = verified FSD detection bit
    TEST_ASSERT_TRUE(isFSDSelectedInUI(f));
}

void test_hw3_dashboard_does_not_inject_builtin_when_ui_bit_clear()
{
    HW3Handler handler;
    handler.enablePrint = false;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x00;

    handler.handleMessage(f, mock);

    TEST_ASSERT_FALSE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_bypass_tlssc_build_flag_no_longer_forces_ui_bit_clear);
    RUN_TEST(test_ui_bit6_selects_ad);
    RUN_TEST(test_hw3_dashboard_does_not_inject_builtin_when_ui_bit_clear);

    return UNITY_END();
}
