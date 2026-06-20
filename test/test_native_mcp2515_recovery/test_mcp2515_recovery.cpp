#include <unity.h>

#include "drivers/mcp2515_driver.h"

uint32_t fakeMillis = 0;
uint32_t fakeDelayMs = 0;

int MCP2515::resetCount = 0;
int MCP2515::bitrateCount = 0;
int MCP2515::normalModeCount = 0;
int MCP2515::errorFlagsReadCount = 0;
int MCP2515::lastSpeed = 0;
int MCP2515::lastClock = 0;
MCP2515::ERROR MCP2515::bitrateResult = MCP2515::ERROR_OK;
MCP2515::ERROR MCP2515::readResult = MCP2515::ERROR_NOMSG;
MCP2515::ERROR MCP2515::sendResult = MCP2515::ERROR_OK;
uint8_t MCP2515::errorFlags = 0;
can_frame MCP2515::nextReadFrame = {};
can_frame MCP2515::lastSentFrame = {};

void setUp()
{
    fakeMillis = 0;
    fakeDelayMs = 0;
    MCP2515::resetFake();
}

void tearDown() {}

void test_recovers_after_consecutive_send_failures()
{
    MCP2515Driver driver(10);
    TEST_ASSERT_TRUE(driver.init());

    MCP2515::sendResult = MCP2515::ERROR_FAILTX;
    CanFrame frame = {};
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_FALSE(driver.send(frame));

    TEST_ASSERT_EQUAL(1, MCP2515::resetCount);

    TEST_ASSERT_FALSE(driver.send(frame));
    TEST_ASSERT_EQUAL(2, MCP2515::resetCount);
    TEST_ASSERT_EQUAL(2, MCP2515::bitrateCount);
    TEST_ASSERT_EQUAL(2, MCP2515::normalModeCount);
    TEST_ASSERT_EQUAL(CAN_500KBPS, MCP2515::lastSpeed);
    TEST_ASSERT_EQUAL(MCP_16MHZ, MCP2515::lastClock);
    TEST_ASSERT_EQUAL(10, fakeDelayMs);
}

void test_recovers_when_error_flags_report_bus_off()
{
    MCP2515Driver driver(10);
    TEST_ASSERT_TRUE(driver.init());

    fakeMillis = 1000;
    MCP2515::readResult = MCP2515::ERROR_NOMSG;
    MCP2515::errorFlags = MCP2515::EFLG_TXBO;

    CanFrame frame = {};
    TEST_ASSERT_FALSE(driver.read(frame));

    TEST_ASSERT_EQUAL(1, MCP2515::errorFlagsReadCount);
    TEST_ASSERT_EQUAL(2, MCP2515::resetCount);
    TEST_ASSERT_EQUAL(2, MCP2515::bitrateCount);
    TEST_ASSERT_EQUAL(2, MCP2515::normalModeCount);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_recovers_after_consecutive_send_failures);
    RUN_TEST(test_recovers_when_error_flags_report_bus_off);
    return UNITY_END();
}
