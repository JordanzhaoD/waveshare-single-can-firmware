#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "dash_ap_first_gate.h"
#include "dash_twai_diag.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;

void setUp()
{
    mock.reset();
    enhancedAutopilotRuntime = true;
}

void tearDown() {}

static void markFsdSelectedInUI(CanFrame &frame)
{
    frame.data[4] |= 0x40;
}

static CanFrame hw3Mux1Frame()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);
    return f;
}

static CanFrame hw4Mux1Frame()
{
    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);
    return f;
}

static CanFrame gearFrame(uint8_t gear)
{
    CanFrame f = {.id = 390};
    f.dlc = 8;
    f.data[7] = static_cast<uint8_t>(gear << 3);
    return f;
}

static CanFrame diSystemStatusFrame(uint8_t gear, bool aca)
{
    CanFrame f = {.id = 280};
    f.dlc = 8;
    f.data[2] = static_cast<uint8_t>(gear << 5);
    if (aca)
        f.data[6] = 0x04;
    return f;
}

static CanFrame summonRequestFrame()
{
    CanFrame f = {.id = 1016};
    f.dlc = 8;
    f.data[3] = 0xB0; // SMART_SUMMON
    return f;
}

static void activateAp(CarManagerBase &handler)
{
    CanFrame f = {.id = 921};
    f.data[0] = 0x03; // ACTIVE_1
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(handler.APActive);
    mock.reset();
}

static void setDasApState(CarManagerBase &handler, uint8_t state)
{
    CanFrame f = {.id = 921, .dlc = 1};
    f.data[0] = state & 0x0F;
    handler.handleMessage(f, mock);
    mock.reset();
}

static CanFrame legacyMux0Frame(bool fsdSelected = true)
{
    CanFrame f = {.id = 1006, .dlc = 8};
    f.data[0] = 0x00;
    if (fsdSelected)
        markFsdSelectedInUI(f);
    return f;
}

static bool legacyGateAllowAfter2000(uint32_t nowMs)
{
    return nowMs >= 2000;
}

static bool legacyGateAlwaysBlocks(uint32_t)
{
    return false;
}

// Represents the dashboard state with the AP-First gate DISABLED by the user.
// dashLegacyFsdActivationAllowed() clears transient AP timing and returns true
// before engaged/debounce/Soft Engage checks. Use case: non-8.3.6 cars where
// direct Legacy 0x3EE injection is safe.
static bool legacyGateAlwaysAllow(uint32_t)
{
    return true;
}

static bool denyAD()
{
    return false;
}

static void advanceNativeDiagNowMsUntilNextCallReturns(uint32_t targetNowMs)
{
    while (dashDiagNowMs() + 1 < targetNowMs)
    {
    }
}

struct LegacyApGateHarness
{
    LegacyHandler *handler{nullptr};
    bool canEnabled{true};
    bool otaAllowed{true};
    bool apGateEnabled{true};
    bool instantEnabled{false};
    uint32_t delayMs{2000};
    bool softEngageEnabled{false};
    bool softEngageSent{false};
    bool steerSeen{true};
    uint8_t steerValidity{0};
    int16_t steerAngleX10{0};

    bool allowed(uint32_t nowMs)
    {
        if (!handler)
            return false;
        if (!canEnabled || !otaAllowed)
        {
            handler->clearLegacySteerTiming();
            softEngageSent = false;
            return false;
        }
        if (!apGateEnabled)
            return true;
        // Push toggle/debounce config (no Minimal in this harness), then read the
        // unified Legacy steer-jerk verdict (settle / Instant edge bypass).
        handler->applyLegacySteerConfig(instantEnabled, false, delayMs);
        DashLegacySteerDecision ap = handler->decideLegacySteer(nowMs);
        if (!ap.allowed)
            return false;

        const bool release = dashSoftEngageRelease(
            softEngageEnabled, softEngageSent,
            steerSeen, steerValidity, steerAngleX10,
            true, false, 50);
        if (!release)
            return false;

        softEngageSent = true;
        return true;
    }
};

static LegacyApGateHarness legacyApGateHarness;

static bool legacyApGateAllowed(uint32_t nowMs)
{
    return legacyApGateHarness.allowed(nowMs);
}

static void configureLegacyApGate(LegacyHandler &handler,
                                  bool instantEnabled,
                                  uint32_t delayMs = 2000)
{
    legacyApGateHarness = LegacyApGateHarness{};
    legacyApGateHarness.handler = &handler;
    legacyApGateHarness.instantEnabled = instantEnabled;
    legacyApGateHarness.delayMs = delayMs;
    handler.legacyFsdActivationAllowed = legacyApGateAllowed;
}

void test_legacy_ap_first_state2_stays_blocked_with_instant_enabled()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);

    setDasApState(handler, 2);
    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_FALSE(handler.legacySteerDiag(dashDiagNowMs()).apEngaged);
}

void test_legacy_ap_first_real_edge_bypasses_delay_once()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);

    setDasApState(handler, 2);
    setDasApState(handler, 3);

    CanFrame first = legacyMux0Frame();
    handler.handleMessage(first, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacySteerDiag(dashDiagNowMs()).instantBypassCount);

    mock.reset();
    CanFrame sustained = legacyMux0Frame();
    handler.handleMessage(sustained, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacySteerDiag(dashDiagNowMs()).instantBypassCount);
}

void test_legacy_ap_first_instant_disabled_waits_default_2000ms()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, false, 2000);

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    const uint32_t edgeMs = handler.legacySteerDiag(dashDiagNowMs()).lastApEdgeMs;

    advanceNativeDiagNowMsUntilNextCallReturns(edgeMs + 1999);
    // Refresh the DAS timestamp WITHOUT advancing the native clock: dashDiagNowMs()
    // is `++static` in native builds, so setDasApState()'s handleMessage() would
    // burn ticks and push the clock past the settle boundary. observeLegacySteer()
    // takes an explicit nowMs, keeping the DAS-freshness gate transparent so this
    // still exercises the settle debounce (1999 ms < 2000 ms -> blocked).
    handler.observeLegacySteer(3, edgeMs + 1999);
    CanFrame before = legacyMux0Frame();
    handler.handleMessage(before, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    advanceNativeDiagNowMsUntilNextCallReturns(edgeMs + 2000);
    handler.observeLegacySteer(3, edgeMs + 2000); // same: refresh DAS, no clock burn
    CanFrame boundary = legacyMux0Frame();
    handler.handleMessage(boundary, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_legacy_ap_first_instant_disabled_waits_custom_1000ms()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, false, 1000);

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    const uint32_t edgeMs = handler.legacySteerDiag(dashDiagNowMs()).lastApEdgeMs;

    advanceNativeDiagNowMsUntilNextCallReturns(edgeMs + 999);
    handler.observeLegacySteer(3, edgeMs + 999); // refresh DAS, no clock burn (see default_2000ms test)
    CanFrame before = legacyMux0Frame();
    handler.handleMessage(before, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    advanceNativeDiagNowMsUntilNextCallReturns(edgeMs + 1000);
    handler.observeLegacySteer(3, edgeMs + 1000);
    CanFrame boundary = legacyMux0Frame();
    handler.handleMessage(boundary, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_legacy_ap_first_state8_and_9_clear_stale_edge()
{
    for (uint8_t state : {static_cast<uint8_t>(8), static_cast<uint8_t>(9)})
    {
        mock.reset();
        LegacyHandler handler;
        handler.enablePrint = false;
        configureLegacyApGate(handler, true);

        setDasApState(handler, 2);
        setDasApState(handler, 3);
        TEST_ASSERT_TRUE(handler.legacySteerDiag(dashDiagNowMs()).edgePending);

        setDasApState(handler, state);
        DashLegacySteerDiag diag = handler.legacySteerDiag(dashDiagNowMs());
        TEST_ASSERT_FALSE(diag.apEngaged);
        TEST_ASSERT_FALSE(diag.edgePending);

        CanFrame mux0 = legacyMux0Frame();
        handler.handleMessage(mux0, mock);
        TEST_ASSERT_EQUAL(0, mock.sent.size());
    }
}

void test_legacy_ap_first_observes_only_primary_das_status()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);

    setDasApState(handler, 2);

    CanFrame party = {.id = 921, .dlc = 1};
    party.bus = CAN_BUS_PARTY;
    party.data[0] = 3;
    handler.handleMessage(party, mock);
    DashLegacySteerDiag afterParty = handler.legacySteerDiag(dashDiagNowMs());
    TEST_ASSERT_FALSE(afterParty.apEngaged);
    TEST_ASSERT_EQUAL_UINT32(0, afterParty.apEdgeCount);

    setDasApState(handler, 3);
    DashLegacySteerDiag afterPrimary = handler.legacySteerDiag(dashDiagNowMs());
    TEST_ASSERT_TRUE(afterPrimary.apEngaged);
    TEST_ASSERT_EQUAL_UINT32(1, afterPrimary.apEdgeCount);

    handler.handleMessage(party, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.legacySteerDiag(dashDiagNowMs()).apEdgeCount);
}

void test_legacy_ap_first_checkad_blocks_final_send()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);
    handler.checkAD = denyAD;

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    DashLegacySteerDiag diag = handler.legacySteerDiag(dashDiagNowMs());
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_TRUE(diag.instantBypassLast);
    TEST_ASSERT_EQUAL_UINT32(1, diag.instantBypassCount);
}

void test_legacy_ap_first_can_off_clears_timing_and_blocks_send()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    legacyApGateHarness.canEnabled = false;

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_FALSE(handler.legacySteerDiag(dashDiagNowMs()).edgePending);
}

void test_legacy_ap_first_ota_block_clears_timing_and_blocks_send()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    legacyApGateHarness.otaAllowed = false;

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_FALSE(handler.legacySteerDiag(dashDiagNowMs()).edgePending);
}

void test_legacy_ap_first_abort_guard_blocks_until_available_rearms()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);
    handler.abortGuard.setEnabled(true);

    setDasApState(handler, 8);
    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    DashLegacySteerDiag diag = handler.legacySteerDiag(dashDiagNowMs());
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_EQUAL_UINT32(0, diag.instantBypassCount);
    TEST_ASSERT_EQUAL_STRING("legacy_fsd_mux0", handler.abortGuard.diag().lastBlockedPath);
}

void test_legacy_ap_first_soft_engage_off_center_blocks_final_send()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    configureLegacyApGate(handler, true);
    legacyApGateHarness.softEngageEnabled = true;
    legacyApGateHarness.steerAngleX10 = 200;

    setDasApState(handler, 2);
    setDasApState(handler, 3);
    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    DashLegacySteerDiag diag = handler.legacySteerDiag(dashDiagNowMs());
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_TRUE(diag.instantBypassLast);
    TEST_ASSERT_EQUAL_UINT32(1, diag.instantBypassCount);
}

void test_legacy_ap_first_parent_disable_and_runtime_reset_clear_transient_state()
{
    LegacyHandler handler;
    handler.observeLegacySteer(2, 100);
    handler.observeLegacySteer(3, 200);
    TEST_ASSERT_TRUE(handler.legacySteerDiag(200).edgePending);

    // Parent gate disable clears transient timing via clearLegacySteerTiming()
    // (decideLegacySteer itself has no gate-disable branch — that lives in the
    // dashboard callback's apInjectionGate short-circuit).
    handler.clearLegacySteerTiming();
    TEST_ASSERT_FALSE(handler.legacySteerDiag(250).edgePending);

    handler.observeLegacySteer(2, 300);
    handler.observeLegacySteer(3, 400);
    TEST_ASSERT_TRUE(handler.legacySteerDiag(400).edgePending);
    handler.resetLegacySteerRuntime();
    DashLegacySteerDiag reset = handler.legacySteerDiag(500);
    TEST_ASSERT_FALSE(reset.apEngaged);
    TEST_ASSERT_FALSE(reset.edgePending);
    TEST_ASSERT_FALSE(reset.instantBypassLast);
}

void test_hw3_enhanced_autopilot_waits_for_ap_before_mux1_injection()
{
    HW3Handler handler;
    handler.enablePrint = false;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    CanFrame beforeAp = hw3Mux1Frame();
    handler.handleMessage(beforeAp, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_FALSE(handler.APActive);
    mock.reset();

    CanFrame stillBeforeAp = hw3Mux1Frame();
    handler.handleMessage(stillBeforeAp, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    activateAp(handler);

    CanFrame afterAp = hw3Mux1Frame();
    handler.handleMessage(afterAp, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);
}

void test_hw3_enhanced_autopilot_allows_mux1_injection_while_parked()
{
    HW3Handler handler;
    handler.enablePrint = false;

    CanFrame park = gearFrame(1);
    handler.handleMessage(park, mock);
    TEST_ASSERT_TRUE(handler.Parked);
    TEST_ASSERT_FALSE(handler.APActive);

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();

    CanFrame whileParked = hw3Mux1Frame();
    handler.handleMessage(whileParked, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);
}

void test_hw3_enhanced_autopilot_stops_mux1_injection_when_shifted_to_drive()
{
    HW3Handler handler;
    handler.enablePrint = false;

    CanFrame park = gearFrame(1);
    handler.handleMessage(park, mock);
    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();
    CanFrame whileParked = hw3Mux1Frame();
    handler.handleMessage(whileParked, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    mock.reset();

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    CanFrame whileDriving = hw3Mux1Frame();
    handler.handleMessage(whileDriving, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw3_summon_request_survives_aca_while_still_in_park()
{
    HW3Handler handler;
    handler.enablePrint = false;

    CanFrame requestBeforeAca = summonRequestFrame();
    handler.handleMessage(requestBeforeAca, mock);

    CanFrame acaPark = diSystemStatusFrame(1, true);
    handler.handleMessage(acaPark, mock);

    CanFrame requestDuringAca = summonRequestFrame();
    handler.handleMessage(requestDuringAca, mock);

    CanFrame stillParkedDuringAca = diSystemStatusFrame(1, true);
    handler.handleMessage(stillParkedDuringAca, mock);

    CanFrame driveDuringAca = diSystemStatusFrame(4, true);
    handler.handleMessage(driveDuringAca, mock);
    TEST_ASSERT_FALSE(handler.Parked);
    TEST_ASSERT_TRUE(handler.Summoning);

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();

    CanFrame whileSummoning = hw3Mux1Frame();
    handler.handleMessage(whileSummoning, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_hw4_enhanced_autopilot_waits_for_ap_before_mux1_injection()
{
    HW4Handler handler;
    handler.enablePrint = false;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    CanFrame beforeAp = hw4Mux1Frame();
    handler.handleMessage(beforeAp, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_FALSE(handler.APActive);
    mock.reset();

    CanFrame stillBeforeAp = hw4Mux1Frame();
    handler.handleMessage(stillBeforeAp, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    activateAp(handler);

    CanFrame afterAp = hw4Mux1Frame();
    handler.handleMessage(afterAp, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL_HEX8(0x80, mock.sent[0].data[5] & 0x80);
}

void test_hw4_enhanced_autopilot_allows_mux1_injection_while_parked()
{
    HW4Handler handler;
    handler.enablePrint = false;

    CanFrame park = gearFrame(1);
    handler.handleMessage(park, mock);
    TEST_ASSERT_TRUE(handler.Parked);
    TEST_ASSERT_FALSE(handler.APActive);

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();

    CanFrame whileParked = hw4Mux1Frame();
    handler.handleMessage(whileParked, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FALSE((mock.sent[0].data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL_HEX8(0x80, mock.sent[0].data[5] & 0x80);
}

void test_hw4_enhanced_autopilot_stops_mux1_injection_when_shifted_to_drive()
{
    HW4Handler handler;
    handler.enablePrint = false;

    CanFrame park = gearFrame(1);
    handler.handleMessage(park, mock);
    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();

    CanFrame whileParked = hw4Mux1Frame();
    handler.handleMessage(whileParked, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    mock.reset();

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    CanFrame whileDriving = hw4Mux1Frame();
    handler.handleMessage(whileDriving, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hw4_summon_request_survives_aca_while_still_in_park()
{
    HW4Handler handler;
    handler.enablePrint = false;

    CanFrame requestBeforeAca = summonRequestFrame();
    handler.handleMessage(requestBeforeAca, mock);

    CanFrame acaPark = diSystemStatusFrame(1, true);
    handler.handleMessage(acaPark, mock);

    CanFrame requestDuringAca = summonRequestFrame();
    handler.handleMessage(requestDuringAca, mock);

    CanFrame stillParkedDuringAca = diSystemStatusFrame(1, true);
    handler.handleMessage(stillParkedDuringAca, mock);

    CanFrame driveDuringAca = diSystemStatusFrame(4, true);
    handler.handleMessage(driveDuringAca, mock);
    TEST_ASSERT_FALSE(handler.Parked);
    TEST_ASSERT_TRUE(handler.Summoning);

    CanFrame observedUiConfig = {.id = 1021};
    observedUiConfig.data[0] = 0x00;
    markFsdSelectedInUI(observedUiConfig);
    handler.handleMessage(observedUiConfig, mock);
    TEST_ASSERT_TRUE(handler.ADEnabled);
    mock.reset();

    CanFrame whileSummoning = hw4Mux1Frame();
    handler.handleMessage(whileSummoning, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_legacy_mux0_waits_for_explicit_activation_gate_even_when_parked()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    handler.legacyFsdActivationAllowed = legacyGateAlwaysBlocks;

    CanFrame park = gearFrame(1);
    handler.handleMessage(park, mock);
    TEST_ASSERT_TRUE(handler.Parked);

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL(FsdGateBlockReason::LegacyFsdSettle, handler.legacyFsdDiag.lastBlockedBy);
    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, handler.legacyFsdDiag.mux0.lastSkip);
}

void test_legacy_mux0_sends_after_explicit_activation_gate_allows()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    handler.legacyFsdActivationAllowed = legacyGateAllowAfter2000;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    CanFrame mux0 = legacyMux0Frame();
    for (int i = 0; i < 1998; ++i)
        (void)dashDiagNowMs();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE((mock.sent[0].data[5] >> 6) & 0x01);
}

// Jordan requirement (2026-06-23): a user on a non-8.3.6 car must be able to
// DISABLE the AP-First gate so Legacy 0x3EE injects directly. With the gate
// disabled (legacyGateAlwaysAllow stub = apInjectionGate=false short-circuit at
// mcp2515_dashboard.h:949), Legacy mux0 must send on the FIRST frame — no
// AP-active, no 2s settle wait. Contrasts the test above (gate ON, waits ~2s).
// See docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md.
void test_legacy_mux0_sends_immediately_when_ap_gate_disabled()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    handler.legacyFsdActivationAllowed = legacyGateAlwaysAllow;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);
    TEST_ASSERT_FALSE(handler.APActive); // AP never engaged — gate disabled permits this

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock); // first frame: no time advance, no 2s wait

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE((mock.sent[0].data[5] >> 6) & 0x01); // bit46 (FSD-enable) set
}

void test_legacy_mux0_blocks_when_das_state_is_available_not_engaged()
{
    LegacyHandler handler;
    handler.enablePrint = false;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    setDasApState(handler, 2); // AP available/off, not engaged
    TEST_ASSERT_FALSE(handler.APActive);

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_TRUE(handler.fsdTriggered);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, handler.legacyFsdDiag.mux0.lastSkip);
}

void test_legacy_mux0_sends_when_das_state_is_engaged_3()
{
    LegacyHandler handler;
    handler.enablePrint = false;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    setDasApState(handler, 3); // AP engaged
    TEST_ASSERT_TRUE(handler.APActive);

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE((mock.sent[0].data[5] >> 6) & 0x01);
}

void test_legacy_mux0_sends_when_das_state_is_engaged_6()
{
    LegacyHandler handler;
    handler.enablePrint = false;

    CanFrame drive = gearFrame(4);
    handler.handleMessage(drive, mock);
    TEST_ASSERT_FALSE(handler.Parked);

    setDasApState(handler, 6); // CN 2026.8.3.6 engaged state
    TEST_ASSERT_TRUE(handler.APActive);

    CanFrame mux0 = legacyMux0Frame();
    handler.handleMessage(mux0, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE((mock.sent[0].data[5] >> 6) & 0x01);
}

void test_legacy_minimal_inject_stops_activation_after_five_and_rearms()
{
    LegacyHandler handler;
    handler.enablePrint = false;
    handler.legacySteerDefense.setMinimalEnabled(true);
    handler.legacySteerDefense.setDebounceMs(2000);

    // Engagement edge arms the 5-frame Minimal burst window (edge-triggered,
    // beta.17). The handler-level mux0 path no longer gates on base
    // minimalInject — the verdict lives in decideLegacySteer() via the dashboard
    // callback — so this exercises the defense directly.
    handler.observeLegacySteer(2, 100);
    handler.observeLegacySteer(3, 200);
    for (uint8_t i = 0; i < kDashMinimalInjectBudget; ++i)
    {
        DashLegacySteerDecision d = handler.legacySteerDefense.decide(200 + i);
        TEST_ASSERT_TRUE(d.minimalBurstOpen);
        TEST_ASSERT_TRUE(d.allowed);
        TEST_ASSERT_TRUE(handler.legacySteerDefense.recordMinimalInjection("legacy_fsd_mux0"));
    }
    TEST_ASSERT_EQUAL_UINT8(kDashMinimalInjectBudget, handler.legacySteerDiag(300).minimalUsed);

    // 6th frame: burst exhausted -> hard stop (settle does not rescue).
    DashLegacySteerDecision blocked = handler.legacySteerDefense.decide(300);
    TEST_ASSERT_FALSE(blocked.allowed);
    TEST_ASSERT_FALSE(handler.legacySteerDefense.recordMinimalInjection("legacy_fsd_mux0"));

    // Disengage + re-engage edge re-arms a fresh 5-frame window.
    handler.observeLegacySteer(2, 400);
    handler.observeLegacySteer(3, 500);
    DashLegacySteerDecision rearmed = handler.legacySteerDefense.decide(500);
    TEST_ASSERT_TRUE(rearmed.minimalBurstOpen);
    TEST_ASSERT_TRUE(rearmed.allowed);
    handler.legacySteerDefense.recordMinimalInjection("legacy_fsd_mux0");
    TEST_ASSERT_EQUAL_UINT8(1, handler.legacySteerDiag(600).minimalUsed);
}

void test_hw3_and_hw4_minimal_inject_stop_only_mux0_activation()
{
    HW3Handler hw3;
    hw3.enablePrint = false;
    hw3.minimalInject.setEnabled(true);
    for (uint8_t i = 0; i < kDashMinimalInjectBudget + 1; ++i)
    {
        CanFrame f = {.id = 1021, .dlc = 8};
        f.data[0] = 0;
        markFsdSelectedInUI(f);
        hw3.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL(kDashMinimalInjectBudget, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, hw3.minimalInject.diag().blocks);

    mock.reset();
    HW4Handler hw4;
    hw4.enablePrint = false;
    hw4.minimalInject.setEnabled(true);
    for (uint8_t i = 0; i < kDashMinimalInjectBudget + 1; ++i)
    {
        CanFrame f = {.id = 1021, .dlc = 8};
        f.data[0] = 0;
        markFsdSelectedInUI(f);
        hw4.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL(kDashMinimalInjectBudget, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, hw4.minimalInject.diag().blocks);
}

void test_legacy_mux0_blocks_when_das_state_is_abort_or_fault()
{
    for (uint8_t state : {static_cast<uint8_t>(8), static_cast<uint8_t>(9)})
    {
        mock.reset();
        LegacyHandler handler;
        handler.enablePrint = false;

        CanFrame drive = gearFrame(4);
        handler.handleMessage(drive, mock);
        TEST_ASSERT_FALSE(handler.Parked);

        setDasApState(handler, 3);
        TEST_ASSERT_TRUE(handler.APActive);

        setDasApState(handler, state);
        TEST_ASSERT_FALSE(handler.APActive);

        CanFrame mux0 = legacyMux0Frame();
        handler.handleMessage(mux0, mock);

        TEST_ASSERT_TRUE(handler.ADEnabled);
        TEST_ASSERT_TRUE(handler.fsdTriggered);
        TEST_ASSERT_EQUAL(0, mock.sent.size());
        TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, handler.legacyFsdDiag.mux0.lastSkip);
    }
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_legacy_ap_first_state2_stays_blocked_with_instant_enabled);
    RUN_TEST(test_legacy_ap_first_real_edge_bypasses_delay_once);
    RUN_TEST(test_legacy_ap_first_instant_disabled_waits_default_2000ms);
    RUN_TEST(test_legacy_ap_first_instant_disabled_waits_custom_1000ms);
    RUN_TEST(test_legacy_ap_first_state8_and_9_clear_stale_edge);
    RUN_TEST(test_legacy_ap_first_observes_only_primary_das_status);
    RUN_TEST(test_legacy_ap_first_checkad_blocks_final_send);
    RUN_TEST(test_legacy_ap_first_can_off_clears_timing_and_blocks_send);
    RUN_TEST(test_legacy_ap_first_ota_block_clears_timing_and_blocks_send);
    RUN_TEST(test_legacy_ap_first_abort_guard_blocks_until_available_rearms);
    RUN_TEST(test_legacy_ap_first_soft_engage_off_center_blocks_final_send);
    RUN_TEST(test_legacy_ap_first_parent_disable_and_runtime_reset_clear_transient_state);

    RUN_TEST(test_hw3_enhanced_autopilot_waits_for_ap_before_mux1_injection);
    RUN_TEST(test_hw3_enhanced_autopilot_allows_mux1_injection_while_parked);
    RUN_TEST(test_hw3_enhanced_autopilot_stops_mux1_injection_when_shifted_to_drive);
    RUN_TEST(test_hw3_summon_request_survives_aca_while_still_in_park);
    RUN_TEST(test_hw4_enhanced_autopilot_waits_for_ap_before_mux1_injection);
    RUN_TEST(test_hw4_enhanced_autopilot_allows_mux1_injection_while_parked);
    RUN_TEST(test_hw4_enhanced_autopilot_stops_mux1_injection_when_shifted_to_drive);
    RUN_TEST(test_hw4_summon_request_survives_aca_while_still_in_park);
    RUN_TEST(test_legacy_mux0_waits_for_explicit_activation_gate_even_when_parked);
    RUN_TEST(test_legacy_mux0_sends_after_explicit_activation_gate_allows);
    RUN_TEST(test_legacy_mux0_sends_immediately_when_ap_gate_disabled);
    RUN_TEST(test_legacy_mux0_blocks_when_das_state_is_available_not_engaged);
    RUN_TEST(test_legacy_mux0_sends_when_das_state_is_engaged_3);
    RUN_TEST(test_legacy_mux0_sends_when_das_state_is_engaged_6);
    RUN_TEST(test_legacy_minimal_inject_stops_activation_after_five_and_rearms);
    RUN_TEST(test_hw3_and_hw4_minimal_inject_stop_only_mux0_activation);
    RUN_TEST(test_legacy_mux0_blocks_when_das_state_is_abort_or_fault);

    return UNITY_END();
}
