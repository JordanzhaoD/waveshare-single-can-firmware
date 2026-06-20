#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;
static uint8_t onSendCount = 0;

static void countOnSend(uint8_t /*mux*/, bool /*ok*/)
{
    onSendCount++;
}

template <typename Handler>
static void prepareDashboardHandler(Handler &handler)
{
    handler.enablePrint = false;
    handler.onSend = countOnSend;
}

static void markFsdSelectedInUI(CanFrame &frame)
{
    frame.data[4] |= 0x40;
}

template <typename Handler>
static void primeFsdTriggered(Handler &handler, uint32_t id)
{
    CanFrame f = {.id = id};
    f.data[0] = 0x00;
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    mock.reset();
    onSendCount = 0;
}

void setUp()
{
    mock.reset();
    onSendCount = 0;
    forceActivateRuntime = false;
    isaSpeedChimeSuppressRuntime = true;
    emergencyVehicleDetectionRuntime = true;
    enhancedAutopilotRuntime = true;
    nagKillerRuntime = true;
}

void tearDown() {}

void test_dashboard_legacy_mux0_fsd_selected_injects_activation()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[6] = 0x02; // simulate original speed profile byte from bus
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
    // Stable policy: byte 6 speed profile bits must remain unchanged
    TEST_ASSERT_EQUAL_HEX8(0x02, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_legacy_manual_profile_injects_mux0()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = false;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[6] = 0x00; // original bus data has no profile bits
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    // Stable policy: byte 6 speed profile bits must remain unchanged
    TEST_ASSERT_EQUAL_HEX8(0x00, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_legacy_mux1_does_not_inject_nag_suppression()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1006};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_TRUE((f.data[2] >> 3) & 0x01);
}

void test_dashboard_hw3_mux0_injects_stable_activation()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[3] = 60;
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL_INT(0, handler.speedOffset);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
    TEST_ASSERT_EQUAL_HEX8(0x02, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_hw3_manual_profile_injects_mux0()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = false;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x04, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_hw3_ui_bit_clear_does_not_inject_builtin_activation()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = true;
    handler.speedProfile = 1;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x00;

    handler.handleMessage(f, mock);

    TEST_ASSERT_FALSE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_dashboard_hw3_mux1_injects_nag_clear()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);
    primeFsdTriggered(handler, 1021);
    uint32_t beforeFramesSent = handler.framesSent;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(beforeFramesSent + 1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);
}

void test_dashboard_hw4_mux0_fsd_selected_injects_activation()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    markFsdSelectedInUI(f);

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[5] & 0x40);
    TEST_ASSERT_EQUAL_HEX8(0x18, mock.sent[0].data[7] & 0x18);
}

void test_dashboard_hw4_manual_profile_injects_mux2_after_fsd_trigger()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = false;
    handler.speedProfile = 4;
    primeFsdTriggered(handler, 1021);
    uint32_t beforeFramesSent = handler.framesSent;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    f.data[7] = 0x70;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(beforeFramesSent + 1, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(1, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x40, mock.sent[0].data[7] & 0x70);
}

void test_dashboard_hw4_mux1_does_not_inject_nag_suppression()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_TRUE((f.data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5] & 0x80);
}

void test_dashboard_hw4_isa_suppression_does_not_inject()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 921};
    f.data[1] = 0x00;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[1] & 0x20);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_dashboard_legacy_mux0_fsd_selected_injects_activation);
    RUN_TEST(test_dashboard_legacy_manual_profile_injects_mux0);
    RUN_TEST(test_dashboard_legacy_mux1_does_not_inject_nag_suppression);
    RUN_TEST(test_dashboard_hw3_mux0_injects_stable_activation);
    RUN_TEST(test_dashboard_hw3_manual_profile_injects_mux0);
    RUN_TEST(test_dashboard_hw3_ui_bit_clear_does_not_inject_builtin_activation);
    RUN_TEST(test_dashboard_hw3_mux1_injects_nag_clear);
    RUN_TEST(test_dashboard_hw4_mux0_fsd_selected_injects_activation);
    RUN_TEST(test_dashboard_hw4_manual_profile_injects_mux2_after_fsd_trigger);
    RUN_TEST(test_dashboard_hw4_mux1_does_not_inject_nag_suppression);
    RUN_TEST(test_dashboard_hw4_isa_suppression_does_not_inject);

    return UNITY_END();
}
