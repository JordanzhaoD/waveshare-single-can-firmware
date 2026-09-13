#include <unity.h>

#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "drivers/mock_driver.h"
#include "dash_ap_rerequest_activation.h"

// v1.18 port of the dual-CAN firmware's test_native_ap_rerequest suite
// (4.5.0-beta09 form, 60 tests). All timing/state-machine semantics are
// device-independent and kept byte-for-byte where possible; the only
// adaptations are the single-CAN bus binding (sourceBus = CAN_BUS_ANY —
// the one TWAI bus; CAN_BUS_PARTY stays a foreign bus for the wrong-bus
// tests) and the inlined ap_rerequest_045:: CRC helpers, which replace the
// dual-CAN DashLegacyWheel045Dnd:: include.
//
// Tier 1 single-vehicle caveats carry over: these tests pin the coordinator
// state machine, NOT real-vehicle anti-jerk effectiveness, and 0x045
// visibility on the single-CAN harness still needs its own Tier 0 check.

// Synthetic 0x045 idle templates captured by the wheel-DND work (valid CRC).
// The stalk field position is NOT vehicle-verified: tests use byte 0 / mask
// 0x07 as a synthetic encoding and must not be read as a real bit layout.
static const uint8_t kIdleC[8] = {0x40, 0x21, 0x00, 0x30, 0x00, 0x00, 0xC0, 0xE0};
static const uint8_t kIdleD[8] = {0x40, 0x21, 0x00, 0x30, 0x00, 0x00, 0xD0, 0x2D};
static const uint8_t kIdleE[8] = {0x40, 0x21, 0x00, 0x30, 0x00, 0x00, 0xE0, 0x67};

static ApReRequestProfile synthProfile()
{
    ApReRequestProfile p;
    p.sourceBus = CAN_BUS_ANY; // single CAN bus (dual-CAN binding was VEH)
    p.qualificationWindowMs = 500;
    p.cancelEvidenceTimeoutMs = 3000;
    p.requestSeqTimeoutMs = 2000;
    p.stalkReleaseGapMs = 60;
    p.availableWaitTimeoutMs = 800;
    p.stalkByte = 0;
    p.stalkMask = 0x07;
    p.stalkFwdValue = 1; // user-frozen: FWD = cancel AP
    p.stalkRwdValue = 2; // user-frozen: RWD = request AP
    p.postQualApMask = (1u << 2) | (1u << 3); // user-frozen qualification set {2,3}
    p.cancelEvidenceApMask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 8) | (1u << 9);
    p.cancelFaultApMask = (1u << 14) | (1u << 15);
    p.requestApMask = (1u << 2); // available set {2}: request waits for state 2
    return p;
}

static void arm(DashApReRequestActivation &c, const ApReRequestProfile &p = synthProfile(),
                bool apGateOpen = true)
{
    c.configure(true, p, apGateOpen);
    c.setPermit(true, nullptr);
}

// Feed a fresh idle native template + DAS state; optionally declare driver
// intent, then drive the AP activation edge that starts a round.
static void startRound(DashApReRequestActivation &c, uint32_t now = 1000)
{
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, now);
    c.observeDasStatus(1, CAN_BUS_ANY, now); // full exit observed first
    c.observeDriverIntent(true, now);
    c.observeDasStatus(3, CAN_BUS_ANY, now + 10); // activation edge -> round
}

// Drive through cancel press/release + evidence + request press/release so
// the coordinator reaches WAIT_QUALIFICATION with t_request = reqMs.
static void reachWaitQualification(DashApReRequestActivation &c, uint32_t now = 1000)
{
    startRound(c, now);
    uint8_t out[8];
    ApReRequestAction a = c.nextStalkFrame(now + 20, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, a);
    c.recordStalkTxResult(true, now + 21);
    a = c.nextStalkFrame(now + 21 + 60, out); // release after gap
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, a);
    c.recordStalkTxResult(true, now + 21 + 61);
    // The synthetic release frame is byte-identical to the native idle at the
    // next counter, so the first native inside the 30ms echo window would be
    // swallowed (proven wheel-DND behaviour). Feed the template refresh past
    // the echo window, mirroring the ~100ms native cadence.
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, now + 140);
    c.observeDasStatus(2, CAN_BUS_ANY, now + 150); // cancel evidence
    a = c.nextStalkFrame(now + 160, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, a);
    c.recordStalkTxResult(true, now + 161); // t_request
    a = c.nextStalkFrame(now + 161 + 60, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestRelease, a);
    c.recordStalkTxResult(true, now + 161 + 61);
}

void test_unconfigured_profile_denies_activation_and_starts_nothing()
{
    DashApReRequestActivation c;
    arm(c, ApReRequestProfile{}); // empty profile
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_FALSE(d.profileReady);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, d.phase);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1000);
    c.observeDriverIntent(true, 1000);
    c.observeDasStatus(1, CAN_BUS_ANY, 1000);
    c.observeDasStatus(3, CAN_BUS_ANY, 1010);
    TEST_ASSERT_EQUAL(0, c.diag().round);
    TEST_ASSERT_FALSE(c.activationAllowed(1010));
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1010, out));
}

void test_power_on_inside_ap_never_backfills_driver_intent()
{
    DashApReRequestActivation c;
    arm(c);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1000);
    c.observeDriverIntent(true, 1000); // intent while already inside AP
    c.observeDasStatus(3, CAN_BUS_ANY, 1000); // no exit seen first
    TEST_ASSERT_EQUAL(0, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    TEST_ASSERT_FALSE(c.activationAllowed(1000));
}

void test_round_starts_on_fresh_exit_then_intent_then_activation()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(1, d.round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, d.phase);
}

void test_round_start_blocks_activation_and_all_device_3ee_writes()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    TEST_ASSERT_TRUE(c.inhibitDevice3ee(1100));
    TEST_ASSERT_FALSE(c.activationAllowed(1100));
    TEST_ASSERT_TRUE(c.ownsStalk(1100));
}

void test_wait_intent_does_not_inhibit_speed_offset_path()
{
    DashApReRequestActivation c;
    arm(c);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1000);
    c.observeDriverIntent(true, 1000);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    TEST_ASSERT_FALSE(c.inhibitDevice3ee(1000));
    TEST_ASSERT_FALSE(c.activationAllowed(1000));
    TEST_ASSERT_FALSE(c.ownsStalk(1000));
}

void test_cancel_press_encodes_fwd_from_fresh_native_template()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c); // template kIdleC at t=1000
    uint8_t out[8] = {};
    ApReRequestAction a = c.nextStalkFrame(1030, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, a);
    TEST_ASSERT_EQUAL_HEX8((0x40 & ~0x07) | 0x01, out[0]); // stalk=FWD, other bits kept
    TEST_ASSERT_EQUAL_HEX8(0x21, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x30, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0xD0, out[6]); // counter C+1, low nibble kept
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
    TEST_ASSERT_EQUAL(ApReRequestStep::PressPending, c.diag().step);
}

void test_no_frame_without_native_template()
{
    DashApReRequestActivation c;
    arm(c);
    c.observeDasStatus(1, CAN_BUS_ANY, 1000);
    c.observeDriverIntent(true, 1000);
    c.observeDasStatus(3, CAN_BUS_ANY, 1010); // round without any 0x045 seen
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1020, out));
}

void test_cancel_release_waits_for_gap_then_returns_stalk_to_idle()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1031 + 59, out));
    ApReRequestAction a = c.nextStalkFrame(1031 + 60, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, a);
    TEST_ASSERT_EQUAL_HEX8(0x40, out[0]); // stalk back to IDLE
    TEST_ASSERT_EQUAL_HEX8(0xE0, out[6]); // press counter D + 1 = E
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
}

void test_stale_template_blocks_press()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c); // template at t=1000, fresh window 150ms
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1000 + 151, out));
    c.observeNative045(kIdleD, 8, CAN_BUS_ANY, 1000 + 160);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1000 + 170, out));
}

void test_cancel_evidence_then_request_press_rwd()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1031 + 60, out));
    c.recordStalkTxResult(true, 1031 + 61);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1140); // past echo window
    c.observeDasStatus(2, CAN_BUS_ANY, 1150);         // AVAILABLE = cancel evidence
    TEST_ASSERT_EQUAL(ApReRequestPhase::RequestSequence, c.diag().phase);
    ApReRequestAction a = c.nextStalkFrame(1160, out);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, a);
    TEST_ASSERT_EQUAL_HEX8((0x40 & ~0x07) | 0x02, out[0]); // stalk=RWD
    TEST_ASSERT_EQUAL_HEX8(0xF0, out[6]); // template E counter + 1
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
    c.recordStalkTxResult(true, 1161); // t_request
    TEST_ASSERT_EQUAL(1161, c.diag().requestMs);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestRelease, c.nextStalkFrame(1161 + 60, out));
    c.recordStalkTxResult(true, 1161 + 61);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitQualification, c.diag().phase);
    TEST_ASSERT_TRUE(c.inhibitDevice3ee(1161 + 62));
}

void test_no_cancel_evidence_times_out_locked()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c, 1000); // deadline 1000+3000
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1200);
    c.observeDasStatus(3, CAN_BUS_ANY, 4099); // still active, keep waiting
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
    c.tick(4100); // round start 1010 + 3000 deadline crossed, DAS still fresh
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("cancelEvidenceTimeout", c.diag().reason);
    // No request press ever generated after the failure.
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(4100, out));
    TEST_ASSERT_EQUAL(0, c.diag().requestAttempts);
}

void test_fault_state_during_cancel_evidence_rearms()
{
    // 4.5.0-beta06: apFaultState is vehicle-side — it auto-re-arms to
    // WaitDriverIntent (with exitSeen cleared, the same fence a normal
    // Complete round uses) instead of locking until a switch cycle.
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    c.observeDasStatus(14, CAN_BUS_ANY, 1100); // FAULT
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_STRING("apFaultState", d.reason); // stays visible
    TEST_ASSERT_EQUAL_STRING("apFaultState", d.lastEndedReason);
    TEST_ASSERT_EQUAL(1, d.autoRearms);
    TEST_ASSERT_FALSE(d.exitSeen); // full AP exit required before round 2
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1101, out));
    TEST_ASSERT_FALSE(c.activationAllowed(1101));
}

void test_qualification_at_499ms_but_not_at_500ms()
{
    DashApReRequestActivation c1;
    arm(c1);
    reachWaitQualification(c1, 1000); // t_request = 1161
    c1.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400); // keep template fresh
    c1.observeDasStatus(3, CAN_BUS_ANY, 1600);         // AP in frozen set, fresh
    c1.observeNative3ee(true, CAN_BUS_ANY, 1161 + 499);
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c1.diag().phase);
    TEST_ASSERT_TRUE(c1.activationAllowed(1161 + 499));

    DashApReRequestActivation c2;
    arm(c2);
    reachWaitQualification(c2, 1000);
    c2.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c2.observeDasStatus(3, CAN_BUS_ANY, 1600);
    c2.observeNative3ee(true, CAN_BUS_ANY, 1161 + 500); // half-open window
    // 4.5.0-beta06: windowExpired is vehicle-side — auto re-arm instead of
    // FailedLocked; injection is still denied for this frame.
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c2.diag().phase);
    TEST_ASSERT_EQUAL_STRING("windowExpired", c2.diag().reason);
    TEST_ASSERT_EQUAL(1, c2.diag().autoRearms);
    TEST_ASSERT_FALSE(c2.activationAllowed(1161 + 500));
}

void test_qualification_requires_ap_in_frozen_set()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(6, CAN_BUS_ANY, 1600); // outside {2,3}
    c.observeNative3ee(true, CAN_BUS_ANY, 1500);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("apOutsideWindowSet", c.diag().reason);
}

void test_qualification_requires_stale_das_rejected()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000); // last DAS at t=1100 (ap=2)
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeNative3ee(true, CAN_BUS_ANY, 1500); // DAS age 400ms ok
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c.diag().phase);
}

void test_qualification_requires_driver_intent_bit()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeNative3ee(false, CAN_BUS_ANY, 1200); // UI deselected
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("intentWithdrawn", c.diag().reason);
}

void test_qualifying_frame_same_event_allows_injection_and_clears_inhibit()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550); // after the last DAS stamp
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c.diag().phase);
    TEST_ASSERT_TRUE(c.activationAllowed(1550)); // same event path: inject now
    TEST_ASSERT_FALSE(c.inhibitDevice3ee(1550));
    TEST_ASSERT_FALSE(c.ownsStalk(1550));
}

void test_injection_stops_when_ap_leaves_post_qualification_set()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550);
    TEST_ASSERT_TRUE(c.activationAllowed(1550));
    c.observeDasStatus(5, CAN_BUS_ANY, 1600); // FSD latched outside {2,3}
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
    TEST_ASSERT_FALSE(c.activationAllowed(1600));
}

void test_driver_brake_during_window_rearms()
{
    // 4.5.0-beta06: apExitedDuringWindow is vehicle-side — auto re-arm with
    // the full-exit fence; no injection and no gesture from the re-armed
    // state until a fresh round starts.
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(0, CAN_BUS_ANY, 1200); // full exit during window
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_STRING("apExitedDuringWindow", d.reason);
    TEST_ASSERT_EQUAL_STRING("apExitedDuringWindow", d.lastEndedReason);
    TEST_ASSERT_EQUAL(1, d.autoRearms);
    // The exit itself was observed inside the lock path, but the fence is
    // armed from scratch: exitSeen must be false so a re-armed round never
    // fires while the car is still in a weird state.
    TEST_ASSERT_FALSE(d.exitSeen);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1300, out));
    TEST_ASSERT_FALSE(c.activationAllowed(1300));
}

void test_physical_stalk_input_aborts_handshake()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t physical[8];
    memcpy(physical, kIdleD, 8);
    physical[0] = (physical[0] & ~0x07) | 0x02; // stalk moved (RWD) by driver
    physical[7] = ap_rerequest_045::crc8J1850(physical, 7);
    c.observeNative045(physical, 8, CAN_BUS_ANY, 1050);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("physicalInput", c.diag().reason);
}

void test_own_echo_is_not_physical_input_and_not_template()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    c.observeNative045(out, 8, CAN_BUS_ANY, 1031 + 5); // own echo inside window
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
    TEST_ASSERT_EQUAL(1, c.diag().ownEchoRx);
    // Template counter must still be the pre-TX one (C), not the echoed D.
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1031 + 60, out));
    TEST_ASSERT_EQUAL_HEX8(0xE0, out[6]); // release counter = press counter D + 1
}

void test_counter_conflict_after_tx_fails_round()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c); // template counter C, press will use D
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    // Native jumps past D+1: counter 0x00 in high nibble of byte 6.
    uint8_t jump[8];
    memcpy(jump, kIdleC, 8);
    jump[6] = (jump[6] & 0x0F) | 0x00;
    jump[7] = ap_rerequest_045::crc8J1850(jump, 7);
    c.observeNative045(jump, 8, CAN_BUS_ANY, 1050);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("counterConflict", c.diag().reason);
}

void test_press_tx_rejection_locks_round()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(false, 1031);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("txFailed", c.diag().reason);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1200, out));
}

void test_release_tx_rejection_locks_round()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(false, 1092);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("txFailed", c.diag().reason);
}

void test_request_seq_timeout_when_evidence_but_no_press()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    c.observeDasStatus(2, CAN_BUS_ANY, 1100); // evidence at 1100, deadline 3100
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 3090);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 3095); // template fresh, press eligible
    c.observeDasStatus(2, CAN_BUS_ANY, 3100); // keep DAS fresh for the tick
    c.tick(3101);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("requestSeqTimeout", c.diag().reason);
}

// ── 2026-09-11 Tier 1 CSV fixes ─────────────────────────────────────────────
// Two root causes from the first real-vehicle round: (1) the request press
// fired 1 ms after the cancel release while the car was still in state 1
// (~940 ms to return to state 2; all 17 calibrated good engagements started
// from state 2) — it must now WAIT for the available set; (2) back-to-back
// frames 1 ms apart reused the same rolling counter (press base anchored on
// a stale template) — a fresh press must anchor on the ring-ahead of
// (template, own last TX).

void test_request_press_waits_for_available_state2()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1123); // past the 30ms echo window
    // Cancel landed (state 1) but the car is NOT available yet: no request
    // gesture may leave while we wait for state 2.
    c.observeDasStatus(1, CAN_BUS_ANY, 1130);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("waitAvailable", c.diag().reason);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1131, out));
    TEST_ASSERT_EQUAL(0, c.diag().requestAttempts);
    // State 2 (available) releases the request gesture.
    c.observeDasStatus(2, CAN_BUS_ANY, 1160);
    TEST_ASSERT_EQUAL(ApReRequestPhase::RequestSequence, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("requestArmed", c.diag().reason);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, c.nextStalkFrame(1161, out));
}

void test_available_wait_timeout_locks()
{
    DashApReRequestActivation c;
    arm(c); // availableWaitTimeoutMs = 800
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    c.observeDasStatus(1, CAN_BUS_ANY, 1110); // evidence latched at 1110
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1800); // native cadence
    c.observeDasStatus(1, CAN_BUS_ANY, 1800); // still not available, keep waiting
    c.tick(1909); // 799 ms since evidence: half-open boundary, still waiting
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
    c.tick(1910); // 800 ms: the car never came back available
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("availableWaitTimeout", c.diag().reason);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1911, out));
}

void test_ap_reengaged_during_available_wait_rearms()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    c.recordStalkTxResult(true, 1092);
    // Before any evidence, state 3 just means the cancel has not taken
    // effect yet (observed 161-526 ms): keep waiting, never lock.
    c.observeDasStatus(3, CAN_BUS_ANY, 1100);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("waitCancelEvidence", c.diag().reason);
    // Cancel landed, then AP re-engaged on its own while we waited for the
    // available state: the re-request is pointless. 4.5.0-beta06: this is
    // vehicle-side — auto re-arm (the car simply has AP back; the driver can
    // cancel and try again) instead of a switch-cycle lock.
    c.observeDasStatus(1, CAN_BUS_ANY, 1110);
    TEST_ASSERT_EQUAL_STRING("waitAvailable", c.diag().reason);
    c.observeDasStatus(3, CAN_BUS_ANY, 1150);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_STRING("apActiveDuringWait", d.reason);
    TEST_ASSERT_EQUAL_STRING("apActiveDuringWait", d.lastEndedReason);
    TEST_ASSERT_EQUAL(1, d.autoRearms);
    TEST_ASSERT_FALSE(d.exitSeen);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1200, out));
}

// ── 4.5.0-beta06: vehicle-side auto re-arm + diagnostics + barrier B ──
// The beta05 Tier 1 field cost of FailedLocked for transient vehicle
// conditions (carrier timing, a rejected re-request, an 8/9 cascade) was a
// manual 8.3.6-switch cycle after every hiccup. Vehicle-side reasons now
// re-arm with the full-exit fence; hardware-class reasons still lock.

void test_window_expired_rearms_and_round2_requires_full_exit_and_edge()
{
    // The beta05 f2-round-A shape: no qualifying carrier inside the window.
    // The re-armed coordinator must behave exactly like a completed round —
    // no gesture, no injection, and a new round only after a FULL AP exit +
    // intent + activation edge, with no switch cycling.
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000); // t_request = 1161, window 500ms
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeNative3ee(true, CAN_BUS_ANY, 1161 + 500); // half-open: expired
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_STRING("windowExpired", d.reason);
    TEST_ASSERT_EQUAL_STRING("windowExpired", d.lastEndedReason);
    TEST_ASSERT_EQUAL(1, d.autoRearms);
    TEST_ASSERT_FALSE(d.exitSeen);
    TEST_ASSERT_FALSE(c.activationAllowed(1661));
    // A direct AP bounce without a full exit must not start round 2.
    c.observeDasStatus(3, CAN_BUS_ANY, 1700);
    TEST_ASSERT_EQUAL(1, c.diag().round);
    TEST_ASSERT_EQUAL_STRING("waitExit", c.diag().reason);              // moved on
    TEST_ASSERT_EQUAL_STRING("windowExpired", c.diag().lastEndedReason); // kept
    // Full exit -> intent -> fresh template -> edge starts round 2.
    c.observeDasStatus(1, CAN_BUS_ANY, 1800);
    c.observeDriverIntent(true, 1810);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1820);
    c.observeDasStatus(3, CAN_BUS_ANY, 1900);
    TEST_ASSERT_EQUAL(2, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
}

void test_vehicle_side_locks_rearm_until_cap_then_auto_rearm_limit()
{
    // kVehicleAutoRearmMax = 3 consecutive vehicle-side locks with no
    // completed round between them; the next one degrades to a REAL
    // FailedLocked so a persistently unhappy vehicle cannot loop the
    // cancel/re-request chain forever.
    DashApReRequestActivation c;
    arm(c);
    for (int i = 0; i < 3; ++i)
    {
        const uint32_t t = 1000 + static_cast<uint32_t>(i) * 10000;
        startRound(c, t);
        c.observeDasStatus(14, CAN_BUS_ANY, t + 15); // FAULT -> vehicle-side
        TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
        TEST_ASSERT_EQUAL_STRING("apFaultState", c.diag().reason);
        TEST_ASSERT_EQUAL(i + 1, c.diag().autoRearms);
    }
    // The 4th consecutive vehicle-side lock exceeds the cap: real lock.
    startRound(c, 40000);
    c.observeDasStatus(14, CAN_BUS_ANY, 40015);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("autoRearmLimit", c.diag().reason);
    TEST_ASSERT_EQUAL(3, c.diag().autoRearms); // the capped attempt did not re-arm
    // lastEndedReason still tells the truth about WHAT ended the round.
    TEST_ASSERT_EQUAL_STRING("apFaultState", c.diag().lastEndedReason);
    // Locked means locked: a full exit + intent + edge must NOT restart.
    c.observeDasStatus(1, CAN_BUS_ANY, 50000);
    c.observeDriverIntent(true, 50010);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 50020);
    c.observeDasStatus(3, CAN_BUS_ANY, 50100);
    TEST_ASSERT_EQUAL(4, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
}

void test_completed_round_resets_consecutive_vehicle_lock_budget()
{
    // A completed round proves the chain works end-to-end: the consecutive
    // vehicle-side lock budget starts fresh.
    DashApReRequestActivation c;
    arm(c);
    startRound(c, 1000);
    c.observeDasStatus(14, CAN_BUS_ANY, 1015);
    startRound(c, 11000);
    c.observeDasStatus(14, CAN_BUS_ANY, 11015);
    TEST_ASSERT_EQUAL(2, c.diag().autoRearms);
    // A full successful round (qualify -> ActiveInjection -> leave the set).
    reachWaitQualification(c, 20000); // round 3, t_request = 20161
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 20240);
    c.observeDasStatus(2, CAN_BUS_ANY, 20250);
    c.observeNative3ee(true, CAN_BUS_ANY, 20280); // qualify -> inject
    c.observeDasStatus(5, CAN_BUS_ANY, 20300);    // Complete, budget reset
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
    // Budget fresh: three more vehicle-side locks still re-arm (with the
    // old counter these would have hit the cap at the third).
    for (int i = 0; i < 3; ++i)
    {
        const uint32_t t = 30000 + static_cast<uint32_t>(i) * 10000;
        startRound(c, t);
        c.observeDasStatus(14, CAN_BUS_ANY, t + 15);
        TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    }
    TEST_ASSERT_EQUAL(5, c.diag().autoRearms);
}

void test_hardware_class_locks_still_lock_without_rearm()
{
    // txFailed / counterConflict / permitLost / physicalInput /
    // invalidNative045 / dasStale / round timeouts indicate something the
    // driver should look at: locking stays the fail-closed answer.
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(false, 1031); // txFailed: hardware-class
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, d.phase);
    TEST_ASSERT_EQUAL_STRING("txFailed", d.reason);
    TEST_ASSERT_EQUAL_STRING("txFailed", d.lastEndedReason);
    TEST_ASSERT_EQUAL(0, d.autoRearms);
    c.observeDasStatus(1, CAN_BUS_ANY, 2000);   // full exit does not unlock
    c.observeDriverIntent(true, 2010);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 2020);
    c.observeDasStatus(3, CAN_BUS_ANY, 2100);
    TEST_ASSERT_EQUAL(1, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
}

void test_round2_cancel_evidence_latch_is_fresh()
{
    // beta06 hardening (barrier B): startRound clears the previous round's
    // cancel-evidence latch. A stale latch + an active state observed during
    // round 2's WaitCancelEvidence (before the fresh cancel lands — observed
    // 22-526 ms in the field) used to fire a FALSE apActiveDuringWait lock;
    // beta05 dodged it only because the cancel landed faster than the 0x399
    // cadence. The race was real.
    DashApReRequestActivation c;
    arm(c);
    // Round 1 runs through evidence (latch set at ~1150) to completion.
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550); // qualify -> ActiveInjection
    c.observeDasStatus(1, CAN_BUS_ANY, 1600);    // leave set -> Complete
    // Round 2: full exit -> intent (held from round 1) -> edge. The template
    // lands late enough that the release frame (1811+60) still sees it fresh
    // (age 121ms <= 150ms).
    c.observeDasStatus(1, CAN_BUS_ANY, 1700);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1750);
    c.observeDasStatus(3, CAN_BUS_ANY, 1800);
    TEST_ASSERT_EQUAL(2, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1810, out));
    c.recordStalkTxResult(true, 1811);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1811 + 60, out));
    c.recordStalkTxResult(true, 1811 + 61);
    // Pre-evidence active state (3): with the stale latch this exact sequence
    // locked apActiveDuringWait; fresh latch = "cancel not landed yet".
    c.observeDasStatus(3, CAN_BUS_ANY, 1830);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("waitCancelEvidence", c.diag().reason);
    // The fresh evidence then lands normally.
    c.observeDasStatus(1, CAN_BUS_ANY, 1900);
    TEST_ASSERT_EQUAL_STRING("waitAvailable", c.diag().reason);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitCancelEvidence, c.diag().phase);
}

void test_request_press_counter_never_repeats_release_counter()
{
    // Real-vehicle collision shape (Tier 1 CSV, both failed rounds): the
    // native bus clones our cancel press inside the release gap, so the
    // template counter sits at the press counter when the request press
    // builds its frame right after the release. Anchoring on the template
    // alone repeats the release counter; our own last TX is the newer
    // event, so the anchor must move past it.
    DashApReRequestActivation c;
    arm(c);
    startRound(c); // template kIdleC (counter C) at t=1000
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    TEST_ASSERT_EQUAL_HEX8(0xD0, out[6]); // press counter C+1 = D
    c.recordStalkTxResult(true, 1031);
    // Native follows our press: template advances C -> D.
    uint8_t follow[8];
    memcpy(follow, kIdleC, 8);
    follow[6] = (follow[6] & 0x0F) | 0xD0;
    follow[7] = ap_rerequest_045::crc8J1850(follow, 7);
    c.observeNative045(follow, 8, CAN_BUS_ANY, 1040);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1091, out));
    TEST_ASSERT_EQUAL_HEX8(0xE0, out[6]); // release counter D+1 = E
    c.recordStalkTxResult(true, 1092);    // lastTx = E, no new native after it
    // Available state releases the request press with the template still at
    // D: the press must be F (past our own E), never E again.
    c.observeDasStatus(2, CAN_BUS_ANY, 1110);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, c.nextStalkFrame(1161, out));
    TEST_ASSERT_EQUAL_HEX8(0xF0, out[6]); // template D stale vs newer TX E -> anchor E +1 = F
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
}

void test_stale_last_tx_never_hijacks_next_round_press_anchor()
{
    // beta04 Tier 1 CSV round 2: the previous round's request-release counter
    // still "led" the fresh native template by 1 on the 4-bit ring 83 s
    // later, so the press skipped the counter the car was waiting for (sent 7
    // while the car echoed 6) -> counterConflict lock, release never sent.
    // The ring cannot tell stale from current; time can. After any native
    // template refresh newer than our last TX, a fresh press must anchor
    // template+1, and the car's echo must NOT read as a conflict.
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000); // request release TX at 1222 -> lastTx = 0
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550); // qualify -> ActiveInjection
    c.observeDasStatus(5, CAN_BUS_ANY, 1600); // leaves {2,3} -> Complete
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
    // 83 s of native traffic later the template is E — the stale lastTx 0
    // "leads" it by exactly 1 on the ring (the CSV shape: car at 5, stale 6).
    const uint32_t t2 = 84000;
    c.observeDasStatus(1, CAN_BUS_ANY, t2); // full exit again
    c.observeDriverIntent(true, t2 + 10);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, t2 + 20); // fresh template E
    c.observeDasStatus(3, CAN_BUS_ANY, t2 + 100); // round 2 edge
    TEST_ASSERT_EQUAL(2, c.diag().round);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(t2 + 110, out));
    // template E + 1 = F is what the car is waiting for — never the stale
    // lastTx 0 + 1 = 1 (the beta04 jump that the car ignored).
    TEST_ASSERT_EQUAL_HEX8(0xF0, out[6]);
    TEST_ASSERT_TRUE(out[6] != 0x10);
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
    c.recordStalkTxResult(true, t2 + 111);
    // The car echoes F (its current counter + 1): the await-native check
    // accepts it and round 2 keeps running — no counterConflict lock.
    uint8_t echo[8];
    memcpy(echo, kIdleE, 8);
    echo[6] = (echo[6] & 0x0F) | 0xF0;
    echo[7] = ap_rerequest_045::crc8J1850(echo, 7);
    c.observeNative045(echo, 8, CAN_BUS_ANY, t2 + 150);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
    TEST_ASSERT_EQUAL(0, c.diag().counterConflicts);
    // The release the CSV round 2 never got to send now follows normally.
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(t2 + 171, out));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[6]); // F + 1 wraps the 4-bit ring
    TEST_ASSERT_TRUE(ap_rerequest_045::crc8J1850(out, 7) == out[7]);
}

void test_permit_loss_locks_and_no_automatic_restart()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    c.setPermit(false, "otaGuard");
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("otaGuard", c.diag().reason);
    c.setPermit(true, nullptr);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 2000);
    c.observeDasStatus(1, CAN_BUS_ANY, 2000);
    c.observeDriverIntent(true, 2000);
    c.observeDasStatus(3, CAN_BUS_ANY, 2010);
    TEST_ASSERT_EQUAL(1, c.diag().round); // no second round without reconfigure
    // Reconfigure re-arms from WAIT_DRIVER_INTENT.
    arm(c);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    TEST_ASSERT_EQUAL(0, c.diag().round);
}

void test_wrong_bus_observations_do_not_advance_round()
{
    DashApReRequestActivation c;
    arm(c); // sourceBus = ANY (single CAN bus)
    c.observeNative045(kIdleC, 8, CAN_BUS_PARTY, 1000);
    c.observeDasStatus(1, CAN_BUS_PARTY, 1000);
    c.observeDriverIntent(true, 1000);
    c.observeDasStatus(3, CAN_BUS_PARTY, 1010);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    c.observeDasStatus(1, CAN_BUS_ANY, 1020);
    c.observeDasStatus(3, CAN_BUS_ANY, 1030);
    TEST_ASSERT_EQUAL(1, c.diag().round);
    // The wrong-bus 0x045 above never armed a template: a bound-bus
    // (CAN_BUS_ANY) native frame is required before any synthetic stalk
    // frame can be built.
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1035);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1040, out));
    c.recordStalkTxResult(true, 1041);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(1101, out));
    c.recordStalkTxResult(true, 1102);
    c.observeDasStatus(2, CAN_BUS_ANY, 1110);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, c.nextStalkFrame(1120, out));
    c.recordStalkTxResult(true, 1121);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestRelease, c.nextStalkFrame(1181, out));
    c.recordStalkTxResult(true, 1182);
    c.observeNative3ee(true, CAN_BUS_PARTY, 1150); // wrong bus
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitQualification, c.diag().phase);
}

void test_next_round_after_complete_needs_full_exit_again()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550); // qualify -> ActiveInjection
    c.observeDasStatus(5, CAN_BUS_ANY, 1600); // Complete
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
    // Direct 5 -> 3 bounce must not start round 2.
    c.observeDasStatus(3, CAN_BUS_ANY, 1700);
    TEST_ASSERT_EQUAL(1, c.diag().round);
    // Full exit then activation edge with intent starts round 2.
    c.observeDasStatus(1, CAN_BUS_ANY, 1800);
    c.observeDriverIntent(true, 1810);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 1820);
    c.observeDasStatus(3, CAN_BUS_ANY, 1900);
    TEST_ASSERT_EQUAL(2, c.diag().round);
}

void test_window_survives_uint32_wrap()
{
    DashApReRequestActivation c;
    arm(c);
    const uint32_t t0 = 0xFFFFFF00u;
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, t0);
    c.observeDasStatus(1, CAN_BUS_ANY, t0);
    c.observeDriverIntent(true, t0);
    c.observeDasStatus(3, CAN_BUS_ANY, t0 + 10);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(t0 + 20, out));
    c.recordStalkTxResult(true, t0 + 21);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, c.nextStalkFrame(t0 + 81, out));
    c.recordStalkTxResult(true, t0 + 82);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, t0 + 140); // past echo window
    c.observeDasStatus(2, CAN_BUS_ANY, t0 + 150);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, c.nextStalkFrame(t0 + 160, out));
    c.recordStalkTxResult(true, t0 + 161); // t_request near wrap
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestRelease, c.nextStalkFrame(t0 + 221, out));
    c.recordStalkTxResult(true, t0 + 222);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, t0 + 250);
    c.observeNative3ee(true, CAN_BUS_ANY, t0 + 161 + 499); // 499ms across wrap
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c.diag().phase);
}

void test_das_stale_fails_active_round()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c, 1000);
    c.tick(1000 + 1020); // last DAS at 1010, age 1010ms > 1000ms freshness
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("dasStale", c.diag().reason);
}

void test_diag_queries_have_no_side_effects()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    for (int i = 0; i < 5; ++i)
    {
        (void)c.diag();
        (void)c.inhibitDevice3ee(1100);
        (void)c.activationAllowed(1100);
        (void)c.ownsStalk(1100);
    }
    TEST_ASSERT_EQUAL(1, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1120, out));
}

// ── Wiring-level tests ──────────────────────────────────────────────────────
// The real LegacyHandler mux0/mux1 branches with the re-request callbacks
// installed through the same seams the dashboard uses (reRequestObserve3ee /
// reRequestInhibit3ee / legacyFsdActivationAllowed). Native dashDiagNowMs() is
// a ++static discrete clock; pump() keeps the module timeline monotone with it
// so every coordinator timestamp <= handler time.

static uint32_t clk() { return dashDiagNowMs(); }
static void pump(uint32_t target)
{
    while (dashDiagNowMs() < target) (void)dashDiagNowMs();
}

static DashApReRequestActivation gWireCtrl;

static void wireObserve3ee(bool intentPresent, uint8_t bus, uint32_t nowMs)
{
    gWireCtrl.observeNative3ee(intentPresent, bus, nowMs);
}
static bool wireInhibit3ee() { return gWireCtrl.inhibitDevice3ee(dashDiagNowMs()); }
// Mirrors dashLegacyFsdActivationAllowed's replace-not-chain top branch: when
// the coordinator is active it is the SOLE authority; "false" below stands in
// for the old settle/defense chain to prove the replacement, not imitation.
static bool wireGate(uint32_t nowMs)
{
    if (gWireCtrl.active())
        return gWireCtrl.activationAllowed(nowMs);
    return false;
}
static void installWiring(LegacyHandler &h)
{
    h.reRequestObserve3ee = wireObserve3ee;
    h.reRequestInhibit3ee = wireInhibit3ee;
    h.legacyFsdActivationAllowed = wireGate;
}

static CanFrame makeMux0(bool intent)
{
    CanFrame f = {.id = 1006};
    f.bus = CAN_BUS_ANY;
    f.data[0] = 0x00;              // mux 0
    f.data[4] = intent ? 0x40 : 0; // FSD select (bit 38 = byte4 bit6)
    return f;
}
static CanFrame makeMux1()
{
    CanFrame f = {.id = 1006};
    f.bus = CAN_BUS_ANY;
    f.data[0] = 0x01; // mux 1
    return f;
}
static CanFrame makeDas(uint8_t apState)
{
    CanFrame f = {.id = 921};
    f.bus = CAN_BUS_ANY;
    f.data[0] = static_cast<uint8_t>(apState & 0x0F);
    f.data[5] = 0x00;
    return f;
}
// handleMessage takes non-const lvalue refs; feed through a named copy.
static void feed(LegacyHandler &h, CanDriver &d, const CanFrame &f)
{
    CanFrame frame = f;
    h.handleMessage(frame, d);
}

// Clock-driven drive to CANCEL_SEQUENCE (barrier A active, no window yet).
static void driveToHandshakeW()
{
    uint32_t t = clk();
    gWireCtrl.observeNative045(kIdleC, 8, CAN_BUS_ANY, t);
    gWireCtrl.observeDasStatus(1, CAN_BUS_ANY, t);
    gWireCtrl.observeDriverIntent(true, t);
    pump(t + 10);
    gWireCtrl.observeDasStatus(3, CAN_BUS_ANY, clk()); // activation edge -> round
}

// Clock-driven drive through both stalk gestures to WAIT_QUALIFICATION with a
// fresh AP=2 DAS stamp, ready for a qualifying mux0 frame.
static void driveToWaitQualificationW()
{
    driveToHandshakeW();
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, gWireCtrl.nextStalkFrame(clk(), out));
    gWireCtrl.recordStalkTxResult(true, clk());
    uint32_t t = clk();
    pump(t + 60);
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelRelease, gWireCtrl.nextStalkFrame(clk(), out));
    gWireCtrl.recordStalkTxResult(true, clk());
    t = clk();
    pump(t + 60); // past the 30ms own-echo window
    gWireCtrl.observeNative045(kIdleE, 8, CAN_BUS_ANY, clk());
    gWireCtrl.observeDasStatus(2, CAN_BUS_ANY, clk()); // cancel evidence
    TEST_ASSERT_EQUAL(ApReRequestPhase::RequestSequence, gWireCtrl.diag().phase);
    t = clk();
    pump(t + 10);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestPress, gWireCtrl.nextStalkFrame(clk(), out));
    gWireCtrl.recordStalkTxResult(true, clk()); // t_request
    t = clk();
    pump(t + 60);
    TEST_ASSERT_EQUAL(ApReRequestAction::RequestRelease, gWireCtrl.nextStalkFrame(clk(), out));
    gWireCtrl.recordStalkTxResult(true, clk()); // -> WAIT_QUALIFICATION
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitQualification, gWireCtrl.diag().phase);
    gWireCtrl.observeDasStatus(2, CAN_BUS_ANY, clk()); // fresh DAS, AP in {2,3}
}

void test_barrier_a_blocks_bit46_and_mux1_through_real_handler()
{
    arm(gWireCtrl);
    driveToHandshakeW();
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, gWireCtrl.diag().phase);
    TEST_ASSERT_TRUE(gWireCtrl.inhibitDevice3ee(clk()));

    LegacyHandler h;
    MockDriver m;
    installWiring(h);
    feed(h, m, makeMux0(true));
    TEST_ASSERT_EQUAL(0u, m.sent.size());
    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, h.legacyFsdDiag.mux0.lastSkip);
    TEST_ASSERT_EQUAL(FsdGateBlockReason::ReRequestHandshake, h.legacyFsdDiag.lastBlockedBy);
    TEST_ASSERT_TRUE((bool)h.fsdTriggered); // intent was observed, not sent

    // mux1 device writes are inside barrier A as well.
    h.legacyFsdDiag.policy = LegacyFsdPolicy::TeslaParity;
    feed(h, m, makeMux1());
    TEST_ASSERT_EQUAL(0u, m.sent.size());
    TEST_ASSERT_EQUAL(FsdSkipReason::GateBlocked, h.legacyFsdDiag.mux1.lastSkip);
    TEST_ASSERT_EQUAL(FsdGateBlockReason::ReRequestHandshake, h.legacyFsdDiag.lastBlockedBy);
}

void test_qualifying_mux0_injects_bit46_in_same_handler_event()
{
    arm(gWireCtrl);
    driveToWaitQualificationW();

    LegacyHandler h;
    MockDriver m;
    installWiring(h);
    feed(h, m, makeMux0(true));
    // The observe callback qualified the coordinator BEFORE the gate query, so
    // this very frame carries bit46 (user-frozen same-event injection).
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, gWireCtrl.diag().phase);
    TEST_ASSERT_EQUAL(1u, m.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x40, m.sent[0].data[5] & 0x40);
    TEST_ASSERT_FALSE(gWireCtrl.inhibitDevice3ee(clk()));
}

void test_coordinator_gate_replaces_old_chain_ap2_allows()
{
    arm(gWireCtrl);
    driveToWaitQualificationW();

    LegacyHandler h;
    MockDriver m;
    installWiring(h);
    // Prime the handler with the same AP evidence the coordinator saw: AP=2
    // leaves the old chain's APActive(3..6) precondition false.
    feed(h, m, makeDas(2));
    TEST_ASSERT_FALSE((bool)h.APActive);
    feed(h, m, makeMux0(true)); // qualifies in the same event
    TEST_ASSERT_EQUAL(1u, m.sent.size());
    TEST_ASSERT_TRUE((bool)(m.sent[0].data[5] & 0x40));

    // Next native mux0 while still in the injection set keeps injecting even
    // though APActive stays false — the old chain would have vetoed this.
    feed(h, m, makeMux0(true));
    TEST_ASSERT_EQUAL(2u, m.sent.size());
    TEST_ASSERT_TRUE((bool)(m.sent[1].data[5] & 0x40));
    TEST_ASSERT_FALSE((bool)h.APActive);
}

void test_gate_denies_when_coordinator_inactive_or_out_of_set()
{
    // Inactive coordinator (flag off / unconfigured profile): the replaced
    // seam denies, matching the dashboard's fail-closed default.
    gWireCtrl.configure(false, synthProfile());
    LegacyHandler h;
    MockDriver m;
    installWiring(h);
    feed(h, m, makeDas(3));
    feed(h, m, makeMux0(true));
    TEST_ASSERT_EQUAL(0u, m.sent.size());

    // Active coordinator that LEFT the injection set (AP=5 -> Complete): the
    // sole-authority gate denies even with intent present.
    arm(gWireCtrl);
    driveToWaitQualificationW();
    LegacyHandler h2;
    MockDriver m2;
    installWiring(h2);
    feed(h2, m2, makeMux0(true)); // qualify -> inject
    TEST_ASSERT_EQUAL(1u, m2.sent.size());
    gWireCtrl.observeDasStatus(5, CAN_BUS_ANY, clk()); // leave {2,3} -> Complete
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, gWireCtrl.diag().phase);
    TEST_ASSERT_FALSE(wireGate(clk()));
    feed(h2, m2, makeMux0(true));
    TEST_ASSERT_EQUAL(1u, m2.sent.size()); // no further injection
}

// ── 4.5.0-beta03: AP injection gate as arm-level precondition ──
// The gate is the coordinator's precondition (user-defined framework): gate
// closed = fully inert (Disabled/"apGateOff", no rounds, no interception) and
// the legacy direct path is the legal normal mode; reopening via configure()
// re-arms automatically without cycling the 8.3.6 switch.

void test_ap_gate_closed_disables_and_silences_coordinator()
{
    DashApReRequestActivation c;
    arm(c, synthProfile(), false); // switch on, gate closed
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_TRUE(d.enabled);
    TEST_ASSERT_TRUE(d.profileReady);
    TEST_ASSERT_FALSE(d.apGateOpen);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, d.phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", d.reason);
    TEST_ASSERT_EQUAL(0u, d.round);
    TEST_ASSERT_FALSE(c.active());

    // The full start-round sequence must not start anything with the gate
    // closed (mode switch, not a fault: never FailedLocked).
    startRound(c);
    TEST_ASSERT_EQUAL(0u, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c.diag().phase);

    // Defensive layers are all inert: activation / barrier A / stalk
    // ownership / stalk frames.
    TEST_ASSERT_FALSE(c.activationAllowed(2000));
    TEST_ASSERT_FALSE(c.inhibitDevice3ee(2000));
    TEST_ASSERT_FALSE(c.ownsStalk(2000));
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(2000, out));
}

void test_ap_gate_reopen_via_configure_rearms_without_lock()
{
    DashApReRequestActivation c;
    arm(c, synthProfile(), false);
    TEST_ASSERT_FALSE(c.active());
    // Reopen the gate through configure(): full reset + auto re-arm — the
    // coordinator is armed again WITHOUT cycling the 8.3.6 switch and never
    // enters FailedLocked (gate toggling is a mode switch, not a fault).
    c.configure(true, synthProfile(), true);
    c.setPermit(true, nullptr);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_STRING("waitIntent", d.reason);
    TEST_ASSERT_TRUE(d.apGateOpen);
    TEST_ASSERT_TRUE(c.active());
    // Arming is not an instant round: the next full exit -> intent ->
    // activation edge starts round 1 (state was reset, so the edge must be
    // re-observed even though the same inputs were fed while gated off).
    startRound(c);
    TEST_ASSERT_EQUAL(1u, c.diag().round);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
}

void test_ap_gate_close_mid_round_resets_cleanly_per_phase()
{
    // (a) Closing the gate during CANCEL_SEQUENCE resets the round cleanly.
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    TEST_ASSERT_EQUAL(ApReRequestPhase::CancelSequence, c.diag().phase);
    TEST_ASSERT_TRUE(c.inhibitDevice3ee(2000));
    c.configure(true, synthProfile(), false);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, d.phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", d.reason);
    TEST_ASSERT_EQUAL(0u, d.round);
    TEST_ASSERT_FALSE(c.active());
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(3000, out));
    TEST_ASSERT_FALSE(c.inhibitDevice3ee(3000));

    // (b) Closing the gate during ACTIVE_INJECTION stops injection at once
    // and hands the activation path back (legacy direct mode).
    DashApReRequestActivation c2;
    arm(c2);
    reachWaitQualification(c2, 1000);
    c2.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c2.observeNative3ee(true, CAN_BUS_ANY, 1500);
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c2.diag().phase);
    TEST_ASSERT_TRUE(c2.activationAllowed(1500));
    c2.configure(true, synthProfile(), false);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c2.diag().phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", c2.diag().reason);
    TEST_ASSERT_FALSE(c2.active());
    TEST_ASSERT_FALSE(c2.activationAllowed(1500));
    TEST_ASSERT_FALSE(c2.inhibitDevice3ee(1500));
}

void test_diag_exposes_ap_gate_open()
{
    DashApReRequestActivation c;
    TEST_ASSERT_FALSE(c.diag().apGateOpen); // default-closed before configure
    arm(c, synthProfile(), true);
    TEST_ASSERT_TRUE(c.diag().apGateOpen);
    arm(c, synthProfile(), false);
    TEST_ASSERT_FALSE(c.diag().apGateOpen);
    TEST_ASSERT_TRUE(c.diag().enabled); // switch state is remembered
    TEST_ASSERT_TRUE(c.diag().profileReady);
}

void test_gate_closed_set_permit_never_locks()
{
    // permitLost immunity: with the gate closed the coordinator sits in
    // Disabled (roundActive == false), so permit transitions can never fire
    // the fail-closed lock. This is why the gate must flow through
    // configure() instead of the permit expression.
    DashApReRequestActivation c;
    arm(c, synthProfile(), false);
    c.setPermit(false, "permitLost");
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", c.diag().reason);
    c.setPermit(true, nullptr);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", c.diag().reason);
}

void test_wire_gate_denies_when_ap_gate_closed()
{
    // Wiring-level proof: with the switch on but the gate closed the
    // coordinator does not authorize activation (active() == false). The
    // production behaviour for a closed gate (fall through to the legacy
    // direct path) lives in dashLegacyFsdActivationAllowed's ordering and is
    // pinned by the Python contract test, not by this seam.
    gWireCtrl.configure(true, synthProfile(), false);
    LegacyHandler h;
    MockDriver m;
    installWiring(h);
    feed(h, m, makeDas(3));
    feed(h, m, makeMux0(true));
    TEST_ASSERT_EQUAL(0u, m.sent.size());
    TEST_ASSERT_FALSE(gWireCtrl.active());
    TEST_ASSERT_FALSE(wireGate(clk()));
    TEST_ASSERT_FALSE(gWireCtrl.inhibitDevice3ee(clk()));
}

// ── 4.5.0-beta06: configure() preserves an ACTIVE_INJECTION round ──
// dashApplyRuntimeState re-applies the runtime state on every /config POST;
// with the old unconditional reset a mid-injection POST killed the bit46
// assertion chain (the prime suspect for the beta05 Tier 1 rounds where
// injection stopped while the car still held state 6). Preservation is
// strict: same enable + same gate + byte-identical valid profile, and only
// in ActiveInjection — every real mode change still takes the full reset.

void test_configure_with_identical_params_preserves_active_injection()
{
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000); // round 1, t_request = 1161
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550); // qualify -> ActiveInjection
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c.diag().phase);
    TEST_ASSERT_TRUE(c.activationAllowed(1550));
    // The identical re-apply the dashboard issues on every /config POST:
    // round, epoch, permit and diagnostics all survive.
    c.configure(true, synthProfile(), true);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, d.phase);
    TEST_ASSERT_EQUAL(1u, d.round);
    TEST_ASSERT_EQUAL(2u, d.epoch);
    TEST_ASSERT_EQUAL_STRING("qualified", d.reason);
    TEST_ASSERT_TRUE(d.permit);
    TEST_ASSERT_TRUE(c.activationAllowed(1560));
    // The preserved round still ends by the normal fence (leaving the set).
    c.observeDasStatus(5, CAN_BUS_ANY, 1600);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Complete, c.diag().phase);
}

void test_configure_resets_when_params_or_modes_change()
{
    // Switch OFF: full reset (the user turned the mode off — always legal).
    DashApReRequestActivation c;
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550);
    TEST_ASSERT_EQUAL(ApReRequestPhase::ActiveInjection, c.diag().phase);
    c.configure(false, synthProfile(), true);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c.diag().phase);
    TEST_ASSERT_EQUAL(0u, c.diag().round);
    TEST_ASSERT_EQUAL(1u, c.diag().epoch);
    TEST_ASSERT_EQUAL_STRING("none", c.diag().lastEndedReason); // diag cleared

    // Gate close mid-injection: full reset to Disabled/"apGateOff" (the
    // beta03 semantics; injection stops at once).
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550);
    c.configure(true, synthProfile(), false);
    TEST_ASSERT_EQUAL(ApReRequestPhase::Disabled, c.diag().phase);
    TEST_ASSERT_EQUAL_STRING("apGateOff", c.diag().reason);
    TEST_ASSERT_EQUAL(0u, c.diag().round);
    TEST_ASSERT_FALSE(c.activationAllowed(1560));

    // Byte-different profile: full reset even with switch + gate unchanged.
    arm(c);
    reachWaitQualification(c, 1000);
    c.observeNative045(kIdleE, 8, CAN_BUS_ANY, 1400);
    c.observeDasStatus(2, CAN_BUS_ANY, 1500);
    c.observeNative3ee(true, CAN_BUS_ANY, 1550);
    ApReRequestProfile other = synthProfile();
    other.stalkReleaseGapMs = 61; // one field off -> memcmp differs
    c.configure(true, other, true);
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    TEST_ASSERT_EQUAL(0u, c.diag().round);
    TEST_ASSERT_FALSE(c.activationAllowed(1560));
}

void test_configure_never_preserves_handshake_phase()
{
    // Preservation is ActiveInjection-only: a /config POST during the
    // handshake (CancelSequence here) still takes the full reset. A reset
    // there strands at most a half-gesture whose release the native idle
    // stream completes — the established semantics.
    DashApReRequestActivation c;
    arm(c);
    startRound(c);
    uint8_t out[8];
    TEST_ASSERT_EQUAL(ApReRequestAction::CancelPress, c.nextStalkFrame(1030, out));
    c.recordStalkTxResult(true, 1031);
    c.configure(true, synthProfile(), true); // identical params, wrong phase
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, c.diag().phase);
    TEST_ASSERT_EQUAL(0u, c.diag().round);
    TEST_ASSERT_EQUAL(1u, c.diag().epoch);
    TEST_ASSERT_EQUAL(ApReRequestAction::None, c.nextStalkFrame(1031 + 60, out));
}

// ---- 4.5.0-beta09 P1: WaitDriverIntent vehicle-refusal watch (observational) ----
// Reproduces the 162535 Tier 1 CSV shape: a native RWD press the car
// silently ignored (no state-3..6 edge), coordinator correctly idle.

// Build a native stalk frame from an idle template (stalkValue 0=idle,
// 1=FWD, 2=RWD in the synthetic 0x07-mask encoding) with a valid CRC.
static void stalkFrame(const uint8_t base[8], uint8_t stalkValue, uint8_t out[8])
{
    memcpy(out, base, 8);
    out[0] = (out[0] & ~0x07) | (stalkValue & 0x07);
    out[7] = ap_rerequest_045::crc8J1850(out, 7);
}

void test_rwd_press_then_no_edge_marks_vehicle_refusal()
{
    DashApReRequestActivation c;
    arm(c);
    uint8_t press[8];
    stalkFrame(kIdleD, 2, press); // native RWD press at t=1000
    c.observeNative045(press, 8, CAN_BUS_ANY, 1000);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_TRUE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(1000, d.rwdPressMs);
    TEST_ASSERT_EQUAL_UINT32(1, d.physicalInputRx);
    // One frame short of the window: still pending, not refused.
    c.tick(3499);
    d = c.diag();
    TEST_ASSERT_TRUE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(0, d.vehicleRefusals);
    // Window closes: refusal latches, counter bumps, pending consumed.
    c.tick(3500);
    d = c.diag();
    TEST_ASSERT_FALSE(d.rwdPressPending);
    TEST_ASSERT_TRUE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(1, d.vehicleRefusals);
    // Purely observational: phase machine untouched.
    TEST_ASSERT_EQUAL(ApReRequestPhase::WaitDriverIntent, d.phase);
    TEST_ASSERT_EQUAL_UINT32(0, d.round);
    TEST_ASSERT_EQUAL_UINT32(1, d.epoch);
}

void test_activation_edge_clears_pending_watch_and_prior_refusal()
{
    DashApReRequestActivation c;
    arm(c);
    uint8_t press[8];
    stalkFrame(kIdleD, 2, press);
    c.observeNative045(press, 8, CAN_BUS_ANY, 1000);
    c.tick(3500); // refusal #1
    // Second press (idle frame first so the stalk edge is real again).
    uint8_t press2[8];
    stalkFrame(kIdleC, 2, press2);
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 4000); // idle: prevNativeStalk=0
    c.observeNative045(press2, 8, CAN_BUS_ANY, 4010);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_TRUE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal); // superseded by the new watch
    // The car answers THIS press with an activation edge — even though the
    // round cannot start (no exitSeen/intent here), the vehicle did accept,
    // so the refusal state must clear. Device-side gating is not a refusal.
    c.observeDasStatus(2, CAN_BUS_ANY, 4020);
    c.observeDasStatus(3, CAN_BUS_ANY, 4030);
    d = c.diag();
    TEST_ASSERT_FALSE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(1, d.vehicleRefusals); // count survives
    // And a second full refusal accumulates.
    c.observeNative045(kIdleC, 8, CAN_BUS_ANY, 4100);
    uint8_t press3[8];
    stalkFrame(kIdleD, 2, press3);
    c.observeNative045(press3, 8, CAN_BUS_ANY, 4200);
    c.tick(6700);
    d = c.diag();
    TEST_ASSERT_TRUE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(2, d.vehicleRefusals);
}

void test_held_rwd_rollover_frames_are_one_press_edge()
{
    DashApReRequestActivation c;
    arm(c);
    uint8_t hold1[8], hold2[8];
    stalkFrame(kIdleD, 2, hold1); // first held frame (counter D)
    stalkFrame(kIdleE, 2, hold2); // 10Hz rollover still held (counter E)
    c.observeNative045(hold1, 8, CAN_BUS_ANY, 1010);
    c.observeNative045(hold2, 8, CAN_BUS_ANY, 1030);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_TRUE(d.rwdPressPending);
    TEST_ASSERT_EQUAL_UINT32(1010, d.rwdPressMs); // first edge stamps
    // Refusal therefore times out from the FIRST held frame.
    c.tick(3510);
    d = c.diag();
    TEST_ASSERT_TRUE(d.vehicleRefusal);
}

void test_round_presses_never_start_the_refusal_watch()
{
    DashApReRequestActivation c;
    arm(c);
    startRound(c); // CancelSequence at t=1010
    uint8_t physical[8];
    stalkFrame(kIdleD, 2, physical); // RWD during a round = physicalInput
    c.observeNative045(physical, 8, CAN_BUS_ANY, 1050);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL(ApReRequestPhase::FailedLocked, d.phase);
    TEST_ASSERT_EQUAL_STRING("physicalInput", d.reason);
    TEST_ASSERT_FALSE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal);
    TEST_ASSERT_EQUAL_UINT32(0, d.vehicleRefusals);
}

void test_fwd_press_is_never_a_refusal_watch()
{
    DashApReRequestActivation c;
    arm(c);
    uint8_t fwd[8];
    stalkFrame(kIdleD, 1, fwd); // FWD = cancel; always observable as 6->1
    c.observeNative045(fwd, 8, CAN_BUS_ANY, 1000);
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL_UINT32(1, d.physicalInputRx);
    TEST_ASSERT_FALSE(d.rwdPressPending);
    TEST_ASSERT_FALSE(d.vehicleRefusal);
}

// ---- 4.5.0-beta09 P2: 0x399 high-nibble flag tally (observational) ----
void test_das_flag_tally_counts_all_phases_and_bound_bus_only()
{
    DashApReRequestActivation c;
    // Gate closed => phase Disabled. The tally is phase-blind (it must be:
    // the flag histogram is exactly what we want while nothing else runs).
    // Unlike the dual-CAN firmware (whose profiles only bind VEH/PARTY, so a
    // default-constructed ANY profile could never tally), the single-CAN
    // binding is CAN_BUS_ANY itself: a never-configured instance would also
    // tally CAN_BUS_ANY frames — every timing/encoding field stays
    // unvalidated until configure(), which is what keeps it inert.
    arm(c, synthProfile(), false);
    c.observeDasStatus(0, CAN_BUS_ANY, 100, 0);
    c.observeDasStatus(0, CAN_BUS_ANY, 200, 1);
    c.observeDasStatus(1, CAN_BUS_ANY, 300, 1);
    c.observeDasStatus(1, CAN_BUS_ANY, 400, 4);
    c.observeDasStatus(1, CAN_BUS_PARTY, 500, 8); // wrong bus: not tallied
    ApReRequestDiag d = c.diag();
    TEST_ASSERT_EQUAL_UINT32(1, d.apFlagCounts[0]);
    TEST_ASSERT_EQUAL_UINT32(2, d.apFlagCounts[1]);
    TEST_ASSERT_EQUAL_UINT32(1, d.apFlagCounts[4]);
    TEST_ASSERT_EQUAL_UINT32(0, d.apFlagCounts[8]);
    TEST_ASSERT_EQUAL_UINT8(4, d.lastApFlag);
    TEST_ASSERT_EQUAL_UINT32(400, d.lastApFlagMs);
    // Legacy 3-arg calls (dasFlags defaults to 0) stay valid: tally[0] grows.
    c.observeDasStatus(2, CAN_BUS_ANY, 600);
    TEST_ASSERT_EQUAL_UINT32(2, c.diag().apFlagCounts[0]);
    // configure() resets the histogram along with every other counter.
    arm(c);
    TEST_ASSERT_EQUAL_UINT32(0, c.diag().apFlagCounts[1]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unconfigured_profile_denies_activation_and_starts_nothing);
    RUN_TEST(test_power_on_inside_ap_never_backfills_driver_intent);
    RUN_TEST(test_round_starts_on_fresh_exit_then_intent_then_activation);
    RUN_TEST(test_round_start_blocks_activation_and_all_device_3ee_writes);
    RUN_TEST(test_wait_intent_does_not_inhibit_speed_offset_path);
    RUN_TEST(test_cancel_press_encodes_fwd_from_fresh_native_template);
    RUN_TEST(test_no_frame_without_native_template);
    RUN_TEST(test_cancel_release_waits_for_gap_then_returns_stalk_to_idle);
    RUN_TEST(test_stale_template_blocks_press);
    RUN_TEST(test_cancel_evidence_then_request_press_rwd);
    RUN_TEST(test_no_cancel_evidence_times_out_locked);
    RUN_TEST(test_fault_state_during_cancel_evidence_rearms);
    RUN_TEST(test_qualification_at_499ms_but_not_at_500ms);
    RUN_TEST(test_qualification_requires_ap_in_frozen_set);
    RUN_TEST(test_qualification_requires_stale_das_rejected);
    RUN_TEST(test_qualification_requires_driver_intent_bit);
    RUN_TEST(test_qualifying_frame_same_event_allows_injection_and_clears_inhibit);
    RUN_TEST(test_injection_stops_when_ap_leaves_post_qualification_set);
    RUN_TEST(test_driver_brake_during_window_rearms);
    RUN_TEST(test_physical_stalk_input_aborts_handshake);
    RUN_TEST(test_own_echo_is_not_physical_input_and_not_template);
    RUN_TEST(test_counter_conflict_after_tx_fails_round);
    RUN_TEST(test_press_tx_rejection_locks_round);
    RUN_TEST(test_release_tx_rejection_locks_round);
    RUN_TEST(test_request_seq_timeout_when_evidence_but_no_press);
    RUN_TEST(test_request_press_waits_for_available_state2);
    RUN_TEST(test_available_wait_timeout_locks);
    RUN_TEST(test_ap_reengaged_during_available_wait_rearms);
    RUN_TEST(test_window_expired_rearms_and_round2_requires_full_exit_and_edge);
    RUN_TEST(test_vehicle_side_locks_rearm_until_cap_then_auto_rearm_limit);
    RUN_TEST(test_completed_round_resets_consecutive_vehicle_lock_budget);
    RUN_TEST(test_hardware_class_locks_still_lock_without_rearm);
    RUN_TEST(test_round2_cancel_evidence_latch_is_fresh);
    RUN_TEST(test_request_press_counter_never_repeats_release_counter);
    RUN_TEST(test_stale_last_tx_never_hijacks_next_round_press_anchor);
    RUN_TEST(test_permit_loss_locks_and_no_automatic_restart);
    RUN_TEST(test_wrong_bus_observations_do_not_advance_round);
    RUN_TEST(test_next_round_after_complete_needs_full_exit_again);
    RUN_TEST(test_window_survives_uint32_wrap);
    RUN_TEST(test_das_stale_fails_active_round);
    RUN_TEST(test_diag_queries_have_no_side_effects);
    RUN_TEST(test_barrier_a_blocks_bit46_and_mux1_through_real_handler);
    RUN_TEST(test_qualifying_mux0_injects_bit46_in_same_handler_event);
    RUN_TEST(test_coordinator_gate_replaces_old_chain_ap2_allows);
    RUN_TEST(test_gate_denies_when_coordinator_inactive_or_out_of_set);
    RUN_TEST(test_ap_gate_closed_disables_and_silences_coordinator);
    RUN_TEST(test_ap_gate_reopen_via_configure_rearms_without_lock);
    RUN_TEST(test_ap_gate_close_mid_round_resets_cleanly_per_phase);
    RUN_TEST(test_diag_exposes_ap_gate_open);
    RUN_TEST(test_gate_closed_set_permit_never_locks);
    RUN_TEST(test_wire_gate_denies_when_ap_gate_closed);
    RUN_TEST(test_configure_with_identical_params_preserves_active_injection);
    RUN_TEST(test_configure_resets_when_params_or_modes_change);
    RUN_TEST(test_configure_never_preserves_handshake_phase);
    RUN_TEST(test_rwd_press_then_no_edge_marks_vehicle_refusal);
    RUN_TEST(test_activation_edge_clears_pending_watch_and_prior_refusal);
    RUN_TEST(test_held_rwd_rollover_frames_are_one_press_edge);
    RUN_TEST(test_round_presses_never_start_the_refusal_watch);
    RUN_TEST(test_fwd_press_is_never_a_refusal_watch);
    RUN_TEST(test_das_flag_tally_counts_all_phases_and_bound_bus_only);
    return UNITY_END();
}
