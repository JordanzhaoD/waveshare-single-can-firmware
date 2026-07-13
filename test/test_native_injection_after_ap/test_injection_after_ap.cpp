#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "dash_ap_first_gate.h"
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

// Represents the dashboard state with the AP-First gate DISABLED by the user
// (apInjectionGate=false → dashLegacyFsdActivationAllowed() short-circuits at
// mcp2515_dashboard.h:949, returning true before the AP-active / 2s-settle checks).
// Use case: non-8.3.6 cars where direct Legacy 0x3EE injection is safe.
static bool legacyGateAlwaysAllow(uint32_t)
{
    return true;
}

void test_ap_first_gate_engaged_state_matrix()
{
    for (uint8_t state : {static_cast<uint8_t>(3), static_cast<uint8_t>(4),
                          static_cast<uint8_t>(5), static_cast<uint8_t>(6)})
        TEST_ASSERT_TRUE(DashApFirstGate::isEngagedState(state));

    for (uint8_t state : {static_cast<uint8_t>(0), static_cast<uint8_t>(1),
                          static_cast<uint8_t>(2), static_cast<uint8_t>(7),
                          static_cast<uint8_t>(8), static_cast<uint8_t>(9),
                          static_cast<uint8_t>(15)})
        TEST_ASSERT_FALSE(DashApFirstGate::isEngagedState(state));
}

void test_ap_first_gate_state2_stays_blocked_with_instant_enabled()
{
    DashApFirstGate gate;
    gate.observe(2, 100);

    DashApFirstDecision decision = gate.decide(true, true, 0, 100);
    TEST_ASSERT_FALSE(decision.engaged);
    TEST_ASSERT_FALSE(decision.edgeDetected);
    TEST_ASSERT_FALSE(decision.debounceSatisfied);
    TEST_ASSERT_FALSE(decision.instantBypass);
    TEST_ASSERT_FALSE(decision.allowed);
}

void test_ap_first_gate_startup_engaged_is_baseline_not_edge()
{
    DashApFirstGate gate;
    gate.observe(3, 100);

    DashApFirstDiag diag = gate.diag(100);
    TEST_ASSERT_TRUE(diag.apEngaged);
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_FALSE(diag.hasApEdge);
    TEST_ASSERT_EQUAL_UINT32(0, diag.apEdgeCount);

    DashApFirstDecision decision = gate.decide(true, true, 2000, 100);
    TEST_ASSERT_TRUE(decision.engaged);
    TEST_ASSERT_FALSE(decision.edgeDetected);
    TEST_ASSERT_FALSE(decision.debounceSatisfied);
    TEST_ASSERT_FALSE(decision.instantBypass);
    TEST_ASSERT_FALSE(decision.allowed);
}

void test_ap_first_gate_real_edge_waits_configured_debounce()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);

    DashApFirstDecision before = gate.decide(true, false, 2000, 2199);
    TEST_ASSERT_TRUE(before.engaged);
    TEST_ASSERT_TRUE(before.edgeDetected);
    TEST_ASSERT_FALSE(before.debounceSatisfied);
    TEST_ASSERT_FALSE(before.instantBypass);
    TEST_ASSERT_FALSE(before.allowed);

    DashApFirstDecision atBoundary = gate.decide(true, false, 2000, 2200);
    TEST_ASSERT_TRUE(atBoundary.debounceSatisfied);
    TEST_ASSERT_TRUE(atBoundary.allowed);

    DashApFirstDiag diag = gate.diag(2200);
    TEST_ASSERT_EQUAL_UINT32(1, diag.apEdgeCount);
    TEST_ASSERT_TRUE(diag.hasApEdge);
    TEST_ASSERT_EQUAL_UINT32(200, diag.lastApEdgeMs);
}

void test_ap_first_gate_instant_bypass_is_one_shot_on_real_edge()
{
    DashApFirstGate gate;
    gate.observe(2, 10);
    gate.observe(3, 20);

    DashApFirstDecision first = gate.decide(true, true, 2000, 20);
    TEST_ASSERT_TRUE(first.edgeDetected);
    TEST_ASSERT_FALSE(first.debounceSatisfied);
    TEST_ASSERT_TRUE(first.instantBypass);
    TEST_ASSERT_TRUE(first.allowed);

    DashApFirstDecision repeated = gate.decide(true, true, 2000, 21);
    TEST_ASSERT_FALSE(repeated.edgeDetected);
    TEST_ASSERT_FALSE(repeated.debounceSatisfied);
    TEST_ASSERT_FALSE(repeated.instantBypass);
    TEST_ASSERT_FALSE(repeated.allowed);

    DashApFirstDiag diag = gate.diag(21);
    TEST_ASSERT_FALSE(diag.edgePending);
    TEST_ASSERT_EQUAL_UINT32(1, diag.apDebounceBypassCount);
}

void test_ap_first_gate_pending_edge_can_bypass_after_instant_is_enabled()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);

    DashApFirstDecision disabled = gate.decide(true, false, 2000, 300);
    TEST_ASSERT_TRUE(disabled.edgeDetected);
    TEST_ASSERT_FALSE(disabled.allowed);
    TEST_ASSERT_TRUE(gate.diag(300).edgePending);

    DashApFirstDecision enabled = gate.decide(true, true, 2000, 301);
    TEST_ASSERT_TRUE(enabled.edgeDetected);
    TEST_ASSERT_TRUE(enabled.instantBypass);
    TEST_ASSERT_TRUE(enabled.allowed);
    TEST_ASSERT_FALSE(gate.diag(301).edgePending);
}

void test_ap_first_gate_disengage_clears_timing_and_pending_edge()
{
    for (uint8_t state : {static_cast<uint8_t>(8), static_cast<uint8_t>(9)})
    {
        DashApFirstGate gate;
        gate.observe(2, 100);
        gate.observe(3, 200);
        TEST_ASSERT_TRUE(gate.diag(200).edgePending);

        gate.observe(state, 300);
        DashApFirstDiag diag = gate.diag(300);
        TEST_ASSERT_FALSE(diag.apEngaged);
        TEST_ASSERT_FALSE(diag.edgePending);
        TEST_ASSERT_FALSE(diag.debounceSatisfied);
        TEST_ASSERT_FALSE(gate.decide(true, true, 2000, 300).allowed);
    }
}

void test_ap_first_gate_reengagement_creates_a_new_edge()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);
    TEST_ASSERT_TRUE(gate.decide(true, true, 2000, 200).instantBypass);

    gate.observe(2, 300);
    gate.observe(3, 400);
    DashApFirstDecision second = gate.decide(true, true, 2000, 400);
    TEST_ASSERT_TRUE(second.edgeDetected);
    TEST_ASSERT_TRUE(second.instantBypass);
    TEST_ASSERT_TRUE(second.allowed);
    TEST_ASSERT_EQUAL_UINT32(2, gate.diag(400).apEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(2, gate.diag(400).apDebounceBypassCount);
}

void test_ap_first_gate_parent_disable_clears_transient_timing()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);
    TEST_ASSERT_TRUE(gate.diag(200).edgePending);

    DashApFirstDecision disabled = gate.decide(false, true, 2000, 250);
    TEST_ASSERT_TRUE(disabled.allowed);
    TEST_ASSERT_FALSE(disabled.instantBypass);

    DashApFirstDiag cleared = gate.diag(250);
    TEST_ASSERT_TRUE(cleared.apEngaged);
    TEST_ASSERT_FALSE(cleared.edgePending);
    TEST_ASSERT_FALSE(cleared.debounceSatisfied);

    gate.observe(3, 300);
    DashApFirstDecision reenabled = gate.decide(true, true, 2000, 300);
    TEST_ASSERT_FALSE(reenabled.edgeDetected);
    TEST_ASSERT_FALSE(reenabled.allowed);
}

void test_ap_first_gate_uint32_wrap_preserves_debounce_elapsed_time()
{
    DashApFirstGate gate;
    gate.observe(2, UINT32_MAX - 100);
    gate.observe(3, UINT32_MAX - 50);

    DashApFirstDecision before = gate.decide(true, false, 100, 48);
    TEST_ASSERT_FALSE(before.debounceSatisfied);
    TEST_ASSERT_FALSE(before.allowed);

    DashApFirstDecision atBoundary = gate.decide(true, false, 100, 49);
    TEST_ASSERT_TRUE(atBoundary.debounceSatisfied);
    TEST_ASSERT_TRUE(atBoundary.allowed);
}

void test_ap_first_gate_bypass_counter_requires_unsatisfied_debounce()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);

    DashApFirstDecision settled = gate.decide(true, true, 100, 300);
    TEST_ASSERT_TRUE(settled.debounceSatisfied);
    TEST_ASSERT_FALSE(settled.instantBypass);
    TEST_ASSERT_TRUE(settled.allowed);
    TEST_ASSERT_EQUAL_UINT32(0, gate.diag(300).apDebounceBypassCount);
}

void test_ap_first_gate_reset_runtime_clears_observation_but_preserves_counters()
{
    DashApFirstGate gate;
    gate.observe(2, 100);
    gate.observe(3, 200);
    TEST_ASSERT_TRUE(gate.decide(true, true, 2000, 200).instantBypass);

    gate.resetRuntime();
    DashApFirstDiag reset = gate.diag(300);
    TEST_ASSERT_FALSE(reset.apEngaged);
    TEST_ASSERT_FALSE(reset.edgePending);
    TEST_ASSERT_FALSE(reset.debounceSatisfied);
    TEST_ASSERT_EQUAL_UINT32(1, reset.apEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(1, reset.apDebounceBypassCount);

    gate.observe(3, 400);
    TEST_ASSERT_FALSE(gate.decide(true, true, 2000, 400).edgeDetected);
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

    RUN_TEST(test_ap_first_gate_engaged_state_matrix);
    RUN_TEST(test_ap_first_gate_state2_stays_blocked_with_instant_enabled);
    RUN_TEST(test_ap_first_gate_startup_engaged_is_baseline_not_edge);
    RUN_TEST(test_ap_first_gate_real_edge_waits_configured_debounce);
    RUN_TEST(test_ap_first_gate_instant_bypass_is_one_shot_on_real_edge);
    RUN_TEST(test_ap_first_gate_pending_edge_can_bypass_after_instant_is_enabled);
    RUN_TEST(test_ap_first_gate_disengage_clears_timing_and_pending_edge);
    RUN_TEST(test_ap_first_gate_reengagement_creates_a_new_edge);
    RUN_TEST(test_ap_first_gate_parent_disable_clears_transient_timing);
    RUN_TEST(test_ap_first_gate_uint32_wrap_preserves_debounce_elapsed_time);
    RUN_TEST(test_ap_first_gate_bypass_counter_requires_unsatisfied_debounce);
    RUN_TEST(test_ap_first_gate_reset_runtime_clears_observation_but_preserves_counters);

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
    RUN_TEST(test_legacy_mux0_blocks_when_das_state_is_abort_or_fault);

    return UNITY_END();
}
