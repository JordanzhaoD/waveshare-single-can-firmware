#pragma once

#include <cstdint>
#include <cstring>
#include <mutex>

#include "can_frame_types.h"

// ── 0x045 frame helpers ─────────────────────────────────────────────────
// Inlined from the dual-CAN firmware's dash_legacy_wheel_045_dnd.h (verified
// against 3202/3202 real-vehicle frames): CRC8-J1850 over bytes 0..6 with the
// 4-bit rolling counter in byte6's HIGH nibble. The single-CAN firmware has
// no other 0x045 TX infrastructure (its wheel DND is 0x3C2, additive
// checksum — different frame, never shares this counter).
namespace ap_rerequest_045
{
inline uint8_t crc8J1850(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x1D)
                               : static_cast<uint8_t>(crc << 1);
    }
    return static_cast<uint8_t>(crc ^ 0xFF);
}

inline bool validFrame(const uint8_t data[8])
{
    return crc8J1850(data, 7) == data[7];
}

inline uint8_t counter(const uint8_t data[8])
{
    return static_cast<uint8_t>((data[6] >> 4) & 0x0F);
}
} // namespace ap_rerequest_045

// v1.18 port of the dual-CAN 4.5.0 (beta01..beta09) AP cancel + re-request
// coordinator ("8.3.6 anti-jerk", design/4.5.0-beta01).
//
// User-frozen design contract (2026-09-10):
//   * 0x045 SpdCtrlLvr_Stat: FWD = cancel AP, RWD = request AP.
//   * 0x3EE confirmation follows the project's original logic: observing a
//     qualified native mux0 frame IS the ECU-sync signal; no separate ACK.
//   * Qualification AP set is {2 AVAILABLE, 3 ACTIVE_NOMINAL} (membership,
//     no 2->3 edge required). A qualified native 0x3EE read inside the
//     t_request + window must begin injection on that same frame.
//
// Everything timing- or encoding-related that is NOT vehicle-verified lives in
// ApReRequestProfile. The compiled default is vehicle-calibrated (see
// compiledApReRequestProfile); the mode switch stays default-off.
enum class ApReRequestAction : uint8_t
{
    None = 0,
    CancelPress,   // 0x045 stalk = FWD (cancel AP)
    CancelRelease, // 0x045 stalk = IDLE after the cancel press
    RequestPress,  // 0x045 stalk = RWD (request AP); accept time = t_request
    RequestRelease // 0x045 stalk = IDLE after the request press
};

enum class ApReRequestPhase : uint8_t
{
    Disabled,
    WaitDriverIntent, // armed; needs full AP exit + intent + activation edge
    CancelSequence,
    WaitCancelEvidence,
    RequestSequence,
    WaitQualification, // t_request .. t_request+window, half-open
    ActiveInjection,   // qualified: device may modify qualifying mux0 frames
    Complete,          // round ended normally; next round needs new intent
    FailedLocked       // anomaly (hardware-class, or vehicle-side past the
                       // re-arm cap); stays locked until configure() re-arms.
                       // Vehicle-side reasons below the cap auto-rearm to
                       // WaitDriverIntent instead (4.5.0-beta06).
};

enum class ApReRequestStep : uint8_t
{
    Idle,
    PressPending,   // frame handed to the driver layer, result unknown
    PressAccepted,  // waiting the release gap
    ReleasePending, // release frame handed to the driver layer
    ReleaseAccepted // gesture complete
};

struct ApReRequestProfile
{
    // Single-CAN standalone is a pure-TWAI single bus: every frame the app
    // loop dispatches carries bus=CAN_BUS_ANY (0), so the compiled profile
    // binds CAN_BUS_ANY here. VEH/PARTY remain valid for tests that model
    // the dual-bus firmware.
    uint8_t sourceBus = CAN_BUS_ANY;
    uint32_t qualificationWindowMs = 0;   // 0 = unconfigured
    uint32_t cancelEvidenceTimeoutMs = 0; // round start -> cancel evidence
    uint32_t requestSeqTimeoutMs = 0;     // evidence -> request press accepted
    uint16_t stalkReleaseGapMs = 0;       // press accepted -> release frame
    // 2026-09-11 Tier 1 CSV (dual-CAN vehicle): the vehicle stays in state 1
    // for ~940 ms after accepting our cancel before returning to state 2,
    // and all 17 calibrated successful RWD engagements started from state 2.
    // The request press WAITS for an available state (requestApMask) instead
    // of firing 1 ms after the cancel release, with this timeout guarding
    // the wait.
    uint32_t availableWaitTimeoutMs = 0; // first cancel evidence -> give up
    // Synthetic stalk field position. NOT verified against any vehicle DB:
    // modelsx_intel exports names/enums only, no start bit / length / order.
    uint8_t stalkByte = 0xFF; // 0..7
    uint8_t stalkMask = 0;    // within-byte mask, nonzero
    uint8_t stalkFwdValue = 1;
    uint8_t stalkRwdValue = 2;
    // Post-qualification continuation set (bit n = DAS state n). The frozen
    // qualification set {2,3} is hardcoded below; continuation beyond it is
    // NOT frozen and must stay profile-driven.
    uint16_t postQualApMask = 0;
    uint16_t cancelEvidenceApMask = 0; // states that prove AP ended
    uint16_t cancelFaultApMask = 0;    // states that fail the round
    // States the car must show BEFORE the request press is emitted (the
    // "available" set; distinct from cancelEvidenceApMask which only proves
    // the cancel landed). Empty = unconfigured.
    uint16_t requestApMask = 0;
};

inline const char *apReRequestProfileError(const ApReRequestProfile &p)
{
    // CAN_BUS_ANY is the single-CAN standalone binding (single TWAI bus,
    // zero ambiguity: no PARTY traffic can ever mix in). A default-constructed
    // profile still fails closed below on the timing checks.
    if (p.sourceBus != CAN_BUS_ANY && p.sourceBus != CAN_BUS_VEH &&
        p.sourceBus != CAN_BUS_PARTY)
        return "sourceUnconfigured";
    if (!p.qualificationWindowMs || !p.cancelEvidenceTimeoutMs ||
        !p.requestSeqTimeoutMs || !p.stalkReleaseGapMs ||
        !p.availableWaitTimeoutMs ||
        p.qualificationWindowMs >= 0x80000000U ||
        p.cancelEvidenceTimeoutMs >= 0x80000000U ||
        p.requestSeqTimeoutMs >= 0x80000000U ||
        p.availableWaitTimeoutMs >= 0x80000000U)
        return "timingUnconfigured";
    if (p.stalkByte > 7 || !p.stalkMask)
        return "stalkEncodingUnconfigured";
    if ((p.stalkFwdValue & ~p.stalkMask) || (p.stalkRwdValue & ~p.stalkMask) ||
        p.stalkFwdValue == p.stalkRwdValue || !p.stalkFwdValue || !p.stalkRwdValue)
        return "stalkValuesInvalid";
    if (!p.postQualApMask || p.postQualApMask > 0x7FFF)
        return "postQualSetUnconfigured"; // states 0..14 (SNA=15 excluded here)
    // Both masks are uint16_t, so every state bit 0..15 is representable; only
    // the emptiness check is meaningful here (fault set may include SNA=15).
    if (!p.cancelEvidenceApMask || !p.cancelFaultApMask || !p.requestApMask)
        return "apSetsUnconfigured";
    return nullptr;
}

// Calibrated from the 2026-09-10/11/12 real-vehicle captures (Model X,
// 2026.8.3.6, via the dual-CAN firmware; four calibration CSVs + nine Tier 1
// rounds; 3202/3202 0x045 frames CRC8-J1850 valid, zero device 0x045 TX).
// All values are VEHICLE properties (encoding, timings, AP-state sets) and
// are device-independent — only sourceBus differs for this single-CAN port.
// The anti-jerk EFFECTIVENESS of the cancel/re-request hypothesis remains a
// single-vehicle small-sample observation; the switch stays default-off.
inline ApReRequestProfile compiledApReRequestProfile()
{
    ApReRequestProfile p;
    // Single-CAN standalone: pure TWAI, one bus, frames carry CAN_BUS_ANY.
    p.sourceBus = CAN_BUS_ANY;
    // Native 0x3EE mux0 carrier is 1 Hz (gap median 1000 ms; both driving
    // files: 100% of gaps > 500 ms, only 2.6% > 1200 ms). The frozen
    // candidate 500 ms would cover ~50% of carrier frames -> ~half the
    // rounds window-expire. Widened 1200 -> 2400 ms in 4.5.0-beta06 after
    // the beta05 Tier 1 CSVs: f2 round A hit the structural gap where the
    // only in-window mux0 carrier landed inside the press->release 100 ms
    // gap (RequestSequence cannot inject) and the NEXT mux0 carrier was
    // 2000 ms after t_request — 1200 ms still expired (~12% of rounds).
    // 2400 ms covers the observed worst case with margin; window semantics
    // stay frozen (half-open from t_request). The AP-exit race a longer
    // window could admit is still fenced by observeDasStatus
    // (apExitedDuringWindow fires the moment 0x399 reports the exit), and
    // windowExpired itself now auto-rearms (vehicle-side, beta06).
    p.qualificationWindowMs = 2400;
    // 14 observed cancels: press -> AP<=2 in 161-526 ms (0x399 is 2 Hz, so
    // evidence is quantized at ~500 ms). 1500 ms ~ 3x observed max.
    p.cancelEvidenceTimeoutMs = 1500;
    // Bounds our own gesture emission (press frame + gap + release); the
    // vehicle's AP response is waited for in WaitQualification, not here.
    p.requestSeqTimeoutMs = 1000;
    // Real presses held 1-3 frames (20-229 ms observed); one nominal frame
    // period emulates the shortest well-attested 2-frame press.
    p.stalkReleaseGapMs = 100;
    // Idle byte0 0x40 -> 0x41 (FWD) / 0x42 (RWD): bits 0-1 of byte0.
    // The wheel-DND byte3 guess is FALSIFIED (byte3 constant 0x30 in all
    // four files, never changed by any gesture).
    p.stalkByte = 0;
    p.stalkMask = 0x03;
    // Frozen contract values confirmed on the vehicle: 5+9 FWD presses all
    // 0x41 and all canceled (6->1); 7+10 RWD presses all 0x42 and all
    // engaged (AP 2->3 in 155-236 ms).
    p.stalkFwdValue = 1;
    p.stalkRwdValue = 2;
    // Qualification set {2,3} is frozen in code; continuation adds the
    // engaged family 4..6 (observed post-request 2->3->6; state 6 is this
    // car's normal active-driving state).
    p.postQualApMask = 0x007C; // {2,3,4,5,6}
    // Observed cancel completions: 6->1 (direct) with 2 = AVAILABLE also
    // meaning "not engaged".
    p.cancelEvidenceApMask = 0x0006; // {1,2}
    // Observed hands-on cascade 6->8->9->1 in one capture episode.
    p.cancelFaultApMask = 0x0300; // {8,9}
    // 2026-09-11 Tier 1 CSV: our cancel press fired the request press 1 ms
    // later while the car was still in state 1 — it holds state 1 for
    // ~936-950 ms before returning to state 2, and every one of the 17
    // calibrated successful RWD engagements started from state 2. The
    // request press now waits for state 2 (available) instead.
    p.requestApMask = 0x0004; // {2}
    // ~3x the observed ~940 ms state-1 recovery time; locks fail-closed if
    // the car never comes back available after the cancel landed.
    p.availableWaitTimeoutMs = 3000;
    return p;
}

struct ApReRequestDiag
{
    bool enabled = false;
    bool profileReady = false;
    bool apGateOpen = false;
    bool permit = false;
    ApReRequestPhase phase = ApReRequestPhase::Disabled;
    ApReRequestStep step = ApReRequestStep::Idle;
    const char *reason = "disabled";
    uint32_t round = 0;
    uint32_t epoch = 1;
    uint8_t apState = 0xFF;
    bool dasFresh = false;
    bool exitSeen = false;
    bool intentPresent = false;
    uint32_t roundStartMs = 0;
    uint32_t evidenceMs = 0;
    uint32_t requestMs = 0; // t_request: request press accepted (software time)
    uint32_t windowRemainingMs = 0;
    uint8_t cancelAttempts = 0;
    uint8_t cancelAccepted = 0;
    uint8_t requestAttempts = 0;
    uint8_t requestAccepted = 0;
    bool templateSeen = false;
    bool templateFresh = false;
    uint8_t templateCounter = 0;
    uint8_t lastTxCounter = 0;
    uint32_t ownEchoRx = 0;
    uint32_t otherBusObs = 0;
    uint32_t physicalInputRx = 0;
    uint32_t counterConflicts = 0;
    uint32_t native3eeRx = 0;
    // 4.5.0-beta06 diagnostics: the reason the last round ENDED (any
    // completeRound or failLocked path) survives re-arms and state changes,
    // cleared only by configure(). This is the field answer to the beta05
    // Tier 1 lesson "record the reason before touching any switch" — the
    // device now remembers it for you. autoRearms counts vehicle-side locks
    // that re-armed automatically instead of locking (see failLocked).
    const char *lastEndedReason = "none";
    uint32_t autoRearms = 0;
    const char *lastAction = "none";
    // 4.5.0-beta09 P1 (vehicle-refusal watch): the 2026-09-12 162535 Tier 1
    // CSV showed a native RWD press the car silently ignored (press -> no
    // activation edge for 6 s; coordinator correctly stayed idle but the
    // driver had no way to tell "car declined" from "box broken"). Purely
    // observational — it never gates or injects anything.
    bool vehicleRefusal = false;  // last watched RWD press got no activation edge
    bool rwdPressPending = false; // RWD press seen, refusal window still open
    uint32_t vehicleRefusals = 0; // cumulative since configure()
    uint32_t rwdPressMs = 0;      // stamp of the pending/last watched press (0 = none)
    // 4.5.0-beta09 P2 (0x399 flag tally): the high nibble of 0x399 byte0
    // carries vehicle-side flags (1 seen with hands-on/slow chains, 4 during
    // end-of-session standstill, 5 once after a violent capture). Semantics
    // unknown — tally every observed value so real-vehicle CSVs can decode it.
    uint8_t lastApFlag = 0;
    uint32_t lastApFlagMs = 0;
    uint32_t apFlagCounts[16] = {};
};

class DashApReRequestActivation
{
public:
    static constexpr uint32_t kDasFreshMs = 1000;
    static constexpr uint32_t kNative045MaxAgeMs = 150;
    static constexpr uint32_t kOwnEchoWindowMs = 30;
    // beta09 P1: how long after a WaitDriverIntent-phase native RWD press we
    // wait for a state-3..6 rising edge before declaring the vehicle refused
    // the activation. 162535 CSV: the refused press showed flag1 within 6 ms
    // but no edge for 6 s, while every accepted engagement edged in
    // 155-578 ms — 2500 ms splits the two populations with margin.
    static constexpr uint32_t kVehicleRefusalMs = 2500;

    // apGateOpen is an arm-level precondition set by the dashboard layer
    // (AP injection gate), NOT a runtime safety input: closing it is a mode
    // switch (reset to Disabled / "apGateOff") and reopening via configure()
    // re-arms automatically (WaitDriverIntent) — it never enters FailedLocked.
    // Default true keeps module-only callers (native tests) unchanged; the
    // production call site passes the live gate explicitly.
    void configure(bool enabled, const ApReRequestProfile &profile,
                   bool apGateOpen = true)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 4.5.0-beta06 fix 3: an ACTIVE_INJECTION round now survives the
        // unrelated /config POSTs that dashApplyRuntimeState issues on every
        // runtime-state apply (a mid-injection reset was the prime suspect
        // for the beta05 Tier 1 CSVs where bit46 injection stopped while the
        // car still held state 6). Preserve ONLY when nothing actually
        // changed: same enable, same gate, byte-identical valid profile. Any
        // real mode change (switch off, gate close, different profile) still
        // takes the full reset below. Handshake phases are never preserved —
        // a reset there strands at most a half-gesture whose release the
        // native idle stream completes, which is the established semantics.
        if (enabled && apGateOpen && state_.phase == Phase::ActiveInjection &&
            state_.enabled && state_.profileReady && state_.apGateOpen &&
            !apReRequestProfileError(profile) &&
            memcmp(&state_.profile, &profile, sizeof(profile)) == 0)
        {
            return;
        }
        state_ = {};
        state_.enabled = enabled;
        state_.profile = profile;
        state_.apGateOpen = apGateOpen;
        state_.profileReady = enabled && !apReRequestProfileError(profile);
        state_.phase = enabled ? (state_.profileReady
                                      ? (apGateOpen ? Phase::WaitDriverIntent
                                                    : Phase::Disabled)
                                      : Phase::Disabled)
                               : Phase::Disabled;
        state_.reason = !enabled               ? "disabled"
                        : !state_.profileReady ? apReRequestProfileError(profile)
                        : !apGateOpen          ? "apGateOff"
                                               : "waitIntent";
    }

    void setPermit(bool allowed, const char *deniedReason)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!allowed && roundActive(state_.phase))
            failLocked(deniedReason ? deniedReason : "permitLost");
        state_.permit = allowed;
    }

    // 0x399 DAS_status observation from the bound source bus only.
    // dasFlags (beta09 P2, default 0 = caller has none) is the HIGH nibble
    // of 0x399 byte0 — vehicle-side flags whose semantics are still unknown
    // (working hypotheses from the two Tier 1 CSVs: 1 = driver hand torque,
    // 4 = standstill/unavailable, 5 = post-violent-capture transient). It is
    // tallied on every bound-bus frame in every phase, Disabled included, so
    // future real-vehicle CSVs can decode it against this histogram. Purely
    // observational — it never feeds a gate, lock, or injection decision.
    void observeDasStatus(uint8_t apState, uint8_t bus, uint32_t now,
                          uint8_t dasFlags = 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bus != state_.profile.sourceBus)
        {
            ++state_.otherBusObs;
            return;
        }
        if (dasFlags != state_.lastApFlag)
        {
            state_.lastApFlag = dasFlags;
            state_.lastApFlagMs = now;
        }
        ++state_.apFlagCounts[dasFlags & 0x0F];
        state_.apState = apState;
        state_.lastDasMs = now;
        const bool evidence = state_.profile.cancelEvidenceApMask & (1u << (apState & 0x0F));
        const bool fault = state_.profile.cancelFaultApMask & (1u << (apState & 0x0F));
        const bool active = apState >= 3 && apState <= 6;
        switch (state_.phase)
        {
        case Phase::WaitDriverIntent:
            if (apState <= 1)
            {
                state_.exitSeen = true;
                state_.reason = state_.intentPresent ? "waitApActive" : "waitIntent";
            }
            if (active && !state_.prevApActive)
            {
                // beta09 P1: a state-3..6 rising edge answers any pending
                // RWD refusal watch — the car accepted that press. Clearing
                // vehicleRefusal too: the newest watched press was accepted,
                // so "last watched press refused" no longer holds.
                state_.rwdPressPending = false;
                state_.vehicleRefusal = false;
                if (state_.exitSeen && state_.intentPresent && state_.permit &&
                    state_.profileReady && state_.apGateOpen)
                    startRound(now);
                else if (!state_.exitSeen)
                    state_.reason = "waitExit";
            }
            break;
        case Phase::CancelSequence:
            if (fault) failLocked("apFaultState");
            break;
        case Phase::WaitCancelEvidence:
            if (fault)
                failLocked("apFaultState");
            else if (state_.cancelEvidenceSeen && active)
            {
                // The cancel landed (evidence seen) and AP then re-engaged
                // on its own while we waited for the available state: the
                // re-request is pointless now. Before the first evidence a
                // still-active state 3 just means the cancel has not taken
                // effect yet (observed 161-526 ms) — keep waiting there.
                failLocked("apActiveDuringWait");
            }
            else if (evidence)
            {
                // 2026-09-11 Tier 1 CSV: pressing 1 ms after the cancel
                // release caught the car still in state 1 (~940 ms to
                // return to 2). Latch the evidence, then wait for an
                // available state before emitting the request gesture.
                if (!state_.cancelEvidenceSeen)
                {
                    state_.cancelEvidenceSeen = true;
                    state_.cancelEvidenceMs = now;
                }
                if (state_.profile.requestApMask & (1u << (apState & 0x0F)))
                    enterRequestSequence(now);
                else
                    state_.reason = "waitAvailable";
            }
            break;
        case Phase::RequestSequence:
            if (fault)
                failLocked("apFaultState");
            else if (evidence &&
                     !(state_.profile.requestApMask & (1u << (apState & 0x0F))))
            {
                // Left the available set before/after the request press:
                // the driver intervened; never auto-restart.
                failLocked(state_.step == Step::Idle ? "apExitedBeforeRequest"
                                                     : "apExitedDuringRequest");
            }
            break;
        case Phase::WaitQualification:
            if (fault)
                failLocked("apFaultState");
            else if (!qualificationApAllowed(apState))
                failLocked(apState <= 1 || apState == 8 || apState == 9
                               ? "apExitedDuringWindow"
                               : "apOutsideWindowSet");
            break;
        case Phase::ActiveInjection:
            if (fault)
                failLocked("apFaultState");
            else if (!(state_.profile.postQualApMask & (1u << (apState & 0x0F))))
                completeRound("apLeftInjectionSet");
            break;
        case Phase::Complete:
            // Re-arm: a full exit observed after completion lets the next
            // activation edge start a new round (exit -> intent -> edge).
            // FailedLocked deliberately never re-arms HERE; the beta06
            // vehicle-side auto-rearm enters WaitDriverIntent directly at
            // the failLocked call site (with exitSeen cleared), and
            // hardware-class locks still need configure().
            if (apState <= 1)
            {
                state_.exitSeen = true;
                state_.phase = Phase::WaitDriverIntent;
                state_.reason = state_.intentPresent ? "waitApActive" : "waitIntent";
            }
            break;
        default:
            break;
        }
        state_.prevApActive = active;
    }

    // Driver FSD intent (forced runtime || UI selection), observed per native
    // 0x3EE mux0 frame by the handler before any gate decision.
    void observeDriverIntent(bool present, uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.intentPresent = present;
        if (state_.phase == Phase::WaitDriverIntent && present && state_.exitSeen)
            state_.reason = "waitApActive";
        (void)now;
    }

    // Native 0x045 STW_ACTN_RQ frames from the bound source bus.
    void observeNative045(const uint8_t data[8], uint8_t dlc, uint8_t bus, uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bus != state_.profile.sourceBus)
        {
            ++state_.otherBusObs;
            return;
        }
        if (dlc != 8 || !ap_rerequest_045::validFrame(data))
        {
            if (roundActive(state_.phase)) failLocked("invalidNative045");
            return;
        }
        if (state_.hasFingerprint && now - state_.fingerprintMs <= kOwnEchoWindowMs &&
            memcmp(data, state_.fingerprint, sizeof(state_.fingerprint)) == 0)
        {
            ++state_.ownEchoRx;
            return;
        }
        const uint8_t stalk = data[state_.profile.stalkByte] & state_.profile.stalkMask;
        if (stalk != 0)
        {
            ++state_.physicalInputRx;
            // beta09 P1: while armed with no round (WaitDriverIntent), a
            // native RWD PRESS EDGE starts the vehicle-refusal watch — the
            // 162535 CSV's "won't turn on" was exactly this: one held press
            // (edge, not the 10 Hz held-frame rollovers), no activation
            // edge for 6 s, coordinator correctly silent, driver with no
            // way to tell a declined car from a dead box. Edge detection via
            // prevNativeStalk; own-echo frames exited above so our own
            // gestures never trigger this. FWD is not tracked (a cancel is
            // always observable as 6->1 and never "refused"). Observational
            // only — this changes no gate, timeout, or emission path.
            if (state_.phase == Phase::WaitDriverIntent &&
                stalk == state_.profile.stalkRwdValue &&
                state_.prevNativeStalk != stalk)
            {
                state_.rwdPressPending = true;
                state_.vehicleRefusal = false; // prior watch superseded
                state_.rwdPressMs = now;
            }
            if (handshakeActive(state_.phase))
                failLocked("physicalInput");
            else if (state_.phase == Phase::ActiveInjection)
                completeRound("physicalInput");
            state_.prevNativeStalk = stalk;
            return;
        }
        state_.prevNativeStalk = 0;
        const uint8_t nativeCounter = ap_rerequest_045::counter(data);
        if (state_.awaitNativeAfterTx &&
            (state_.phase == Phase::CancelSequence || state_.phase == Phase::RequestSequence ||
             state_.phase == Phase::WaitCancelEvidence))
        {
            state_.awaitNativeAfterTx = false;
            if (nativeCounter != state_.lastTxCounter &&
                nativeCounter !=
                    static_cast<uint8_t>((state_.lastTxCounter + 1) & 0x0F))
            {
                ++state_.counterConflicts;
                failLocked("counterConflict");
                return;
            }
        }
        memcpy(state_.templateData, data, sizeof(state_.templateData));
        state_.templateMs = now;
        state_.templateSeen = true;
    }

    // Native 0x3EE mux0 observation from the bound source bus. Called by the
    // handler on the same frame BEFORE the activation gate query, so a frame
    // that qualifies may carry bit46 in that same event ("inject immediately").
    void observeNative3ee(bool intentPresent, uint8_t bus, uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bus != state_.profile.sourceBus)
        {
            ++state_.otherBusObs;
            return;
        }
        ++state_.native3eeRx;
        state_.intentPresent = intentPresent;
        if (state_.phase == Phase::WaitQualification)
        {
            if (!intentPresent)
            {
                completeRound("intentWithdrawn");
                return;
            }
            if (withinWindow(now) && dasFreshLocked(now) &&
                qualificationApAllowed(state_.apState))
            {
                state_.phase = Phase::ActiveInjection;
                state_.step = Step::Idle;
                state_.reason = "qualified";
                return;
            }
            if (!withinWindow(now))
                failLocked("windowExpired");
        }
        else if (state_.phase == Phase::ActiveInjection && !intentPresent)
        {
            completeRound("intentWithdrawn");
        }
    }

    void tick(uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // beta09 P1: the refusal watch lives in WaitDriverIntent, which is
        // NOT roundActive — evaluate it BEFORE the early return below. A
        // pending RWD press whose window closes with no activation edge is
        // consumed here: vehicleRefusal latches (until the next press or the
        // next accepted edge) and the counter accumulates. No phase or
        // reason changes — the round machinery is untouched by this.
        if (state_.rwdPressPending && state_.phase == Phase::WaitDriverIntent &&
            now - state_.rwdPressMs >= kVehicleRefusalMs)
        {
            state_.rwdPressPending = false;
            state_.vehicleRefusal = true;
            ++state_.vehicleRefusals;
        }
        if (!roundActive(state_.phase)) return;
        if (!dasFreshLocked(now))
        {
            failLocked("dasStale");
            return;
        }
        switch (state_.phase)
        {
        case Phase::CancelSequence:
            if (now - state_.roundStartMs >= state_.profile.cancelEvidenceTimeoutMs)
                failLocked(state_.step == Step::Idle ? "cancelSeqTimeout"
                                                     : "cancelEvidenceTimeout");
            break;
        case Phase::WaitCancelEvidence:
            if (!state_.cancelEvidenceSeen)
            {
                // Cancel has not provably landed yet (car may still be
                // active — it takes 161-526 ms to leave AP).
                if (now - state_.roundStartMs >= state_.profile.cancelEvidenceTimeoutMs)
                    failLocked("cancelEvidenceTimeout");
            }
            else if (now - state_.cancelEvidenceMs >=
                     state_.profile.availableWaitTimeoutMs)
            {
                // Cancel landed but the car never returned to the available
                // set (~940 ms observed recovery; ~3x guard).
                failLocked("availableWaitTimeout");
            }
            break;
        case Phase::RequestSequence:
            if (now - state_.evidenceMs >= state_.profile.requestSeqTimeoutMs)
                failLocked("requestSeqTimeout");
            break;
        case Phase::WaitQualification:
            if (!withinWindow(now)) failLocked("windowExpired");
            break;
        default:
            break;
        }
    }

    // Produces the next synthetic stalk frame. The caller owns the actual
    // driver submission and must report it via recordStalkTxResult().
    ApReRequestAction nextStalkFrame(uint32_t now, uint8_t outData[8])
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!state_.enabled || !state_.profileReady || !state_.apGateOpen ||
            !state_.permit || !state_.templateSeen ||
            now - state_.templateMs > kNative045MaxAgeMs)
            return ApReRequestAction::None;
        const bool cancel = state_.phase == Phase::CancelSequence;
        const bool request = state_.phase == Phase::RequestSequence;
        if (!cancel && !request) return ApReRequestAction::None;
        if (state_.step == Step::Idle)
        {
            buildFrame(outData, cancel ? state_.profile.stalkFwdValue
                                       : state_.profile.stalkRwdValue);
            state_.pending = cancel ? ApReRequestAction::CancelPress
                                    : ApReRequestAction::RequestPress;
            state_.step = Step::PressPending;
            if (cancel)
                ++state_.cancelAttempts;
            else
                ++state_.requestAttempts;
        }
        else if (state_.step == Step::PressAccepted &&
                 now - state_.pressMs >= state_.profile.stalkReleaseGapMs)
        {
            buildFrame(outData, 0); // stalk back to IDLE
            state_.pending = cancel ? ApReRequestAction::CancelRelease
                                    : ApReRequestAction::RequestRelease;
            state_.step = Step::ReleasePending;
        }
        else
        {
            return ApReRequestAction::None;
        }
        state_.lastAction = actionName(state_.pending);
        return state_.pending;
    }

    void recordStalkTxResult(bool ok, uint32_t now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.step != Step::PressPending && state_.step != Step::ReleasePending)
            return;
        const bool wasPress = state_.step == Step::PressPending;
        if (!ok)
        {
            failLocked("txFailed");
            return;
        }
        state_.lastTxCounter = ap_rerequest_045::counter(state_.builtData);
        state_.lastTxValid = true;
        state_.lastTxMs = now;
        memcpy(state_.fingerprint, state_.builtData, sizeof(state_.fingerprint));
        state_.fingerprintMs = now;
        state_.hasFingerprint = true;
        state_.awaitNativeAfterTx = true;
        if (wasPress)
        {
            state_.pressMs = now;
            state_.step = Step::PressAccepted;
            if (state_.pending == ApReRequestAction::CancelPress)
            {
                ++state_.cancelAccepted;
                state_.reason = "cancelPressAccepted";
            }
            else
            {
                ++state_.requestAccepted;
                state_.requestMs = now; // t_request (software submit-accept time)
                state_.reason = "requestPressAccepted";
            }
        }
        else
        {
            if (state_.pending == ApReRequestAction::CancelRelease)
            {
                ++state_.cancelAccepted;
                state_.step = Step::ReleaseAccepted;
                state_.phase = Phase::WaitCancelEvidence;
                state_.reason = "waitCancelEvidence";
                // Evidence may already have arrived while the gesture ran.
                // Latch it, but only the available set (requestApMask) may
                // start the request gesture — same gating as observeDasStatus.
                const uint8_t ap = state_.apState & 0x0F;
                if (state_.profile.cancelEvidenceApMask & (1u << ap))
                {
                    state_.cancelEvidenceSeen = true;
                    state_.cancelEvidenceMs = now;
                }
                if (state_.profile.requestApMask & (1u << ap))
                    enterRequestSequence(now);
                else if (state_.cancelEvidenceSeen)
                    state_.reason = "waitAvailable";
            }
            else
            {
                ++state_.requestAccepted;
                state_.step = Step::ReleaseAccepted;
                state_.phase = Phase::WaitQualification;
                state_.reason = "waitQualification";
            }
        }
    }

    // Barrier A: while a round is in its handshake, ALL device 0x3EE writes
    // (activation bit46, speed offset, mux1) must be blocked.
    bool inhibitDevice3ee(uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)now;
        return state_.enabled && state_.apGateOpen && handshakeActive(state_.phase);
    }

    // True while the mode is enabled with a valid profile AND the AP injection
    // gate is open (arm-level precondition): the caller must make the
    // coordinator the SOLE activation authority (replace the old
    // settle/defense chain, never chain after it — APActive(3..6) settle would
    // veto the user-frozen AP=2 qualification path). With the gate closed the
    // coordinator is fully inert and the caller's legacy direct path is the
    // legal normal mode (non-8.3.6 vehicles).
    bool active() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.enabled && state_.profileReady && state_.apGateOpen;
    }

    // Sole activation authority while the mode is enabled: the old settle /
    // defense / APActive(3..6) chain is replaced, never chained after this.
    // apGateOpen is checked defensively (active() already gates callers).
    bool activationAllowed(uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.enabled && state_.profileReady && state_.apGateOpen &&
               state_.permit &&
               state_.phase == Phase::ActiveInjection && dasFreshLocked(now) &&
               (state_.profile.postQualApMask & (1u << (state_.apState & 0x0F)));
    }

    // 0x045 ownership arbitration (any future 0x045 writer and this module
    // must not share the frame/counter; the single-CAN wheel DND is 0x3C2
    // and never competes — the hook is kept for parity with the dual-CAN
    // firmware and future 0x045 features).
    bool ownsStalk(uint32_t now) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)now;
        return state_.enabled && state_.apGateOpen && handshakeActive(state_.phase);
    }

    ApReRequestDiag diag() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ApReRequestDiag d;
        d.enabled = state_.enabled;
        d.profileReady = state_.profileReady;
        d.apGateOpen = state_.apGateOpen;
        d.permit = state_.permit;
        d.phase = state_.phase;
        d.step = state_.step;
        d.reason = state_.reason;
        d.round = state_.round;
        d.epoch = state_.epoch;
        d.apState = state_.apState;
        d.exitSeen = state_.exitSeen;
        d.intentPresent = state_.intentPresent;
        d.roundStartMs = state_.roundStartMs;
        d.evidenceMs = state_.evidenceMs;
        d.requestMs = state_.requestMs;
        d.windowRemainingMs =
            (state_.phase == Phase::WaitQualification && withinWindow(state_.requestMs))
                ? static_cast<uint32_t>(state_.profile.qualificationWindowMs -
                                        (diagNowDummyLocked() - state_.requestMs))
                : 0;
        d.cancelAttempts = state_.cancelAttempts;
        d.cancelAccepted = state_.cancelAccepted;
        d.requestAttempts = state_.requestAttempts;
        d.requestAccepted = state_.requestAccepted;
        d.templateSeen = state_.templateSeen;
        d.lastTxCounter = state_.lastTxCounter;
        d.ownEchoRx = state_.ownEchoRx;
        d.otherBusObs = state_.otherBusObs;
        d.physicalInputRx = state_.physicalInputRx;
        d.counterConflicts = state_.counterConflicts;
        d.native3eeRx = state_.native3eeRx;
        d.lastEndedReason = state_.lastEndedReason;
        d.autoRearms = state_.autoRearms;
        d.lastAction = state_.lastAction;
        d.vehicleRefusal = state_.vehicleRefusal;
        d.rwdPressPending = state_.rwdPressPending;
        d.vehicleRefusals = state_.vehicleRefusals;
        // Raw stamps, no clock in diag(): the dashboard layer computes age
        // the same way it does for windowRemainingMs-free consumers.
        d.rwdPressMs = state_.rwdPressMs;
        d.lastApFlag = state_.lastApFlag;
        d.lastApFlagMs = state_.lastApFlagMs;
        memcpy(d.apFlagCounts, state_.apFlagCounts, sizeof(d.apFlagCounts));
        return d;
    }

private:
    using Phase = ApReRequestPhase;
    using Step = ApReRequestStep;

    struct State
    {
        bool enabled = false, profileReady = false, apGateOpen = false, permit = false;
        bool exitSeen = false, intentPresent = false, prevApActive = false;
        bool templateSeen = false, hasFingerprint = false, awaitNativeAfterTx = false;
        ApReRequestProfile profile{};
        Phase phase = Phase::Disabled;
        Step step = Step::Idle;
        const char *reason = "disabled";
        const char *lastEndedReason = "none";
        const char *lastAction = "none";
        ApReRequestAction pending = ApReRequestAction::None;
        uint32_t round = 0, epoch = 1;
        uint8_t apState = 0xFF;
        uint32_t lastDasMs = 0;
        uint32_t roundStartMs = 0, evidenceMs = 0, requestMs = 0, pressMs = 0;
        bool cancelEvidenceSeen = false;
        uint32_t cancelEvidenceMs = 0;
        uint32_t templateMs = 0, fingerprintMs = 0;
        uint8_t templateData[8] = {};
        uint8_t builtData[8] = {};
        uint8_t fingerprint[8] = {};
        uint8_t templateCounter = 0, lastTxCounter = 0;
        bool lastTxValid = false; // lastTxCounter holds a frame WE sent
        uint32_t lastTxMs = 0;    // stamp of that TX (anchor recency, see buildFrame)
        uint8_t cancelAttempts = 0, cancelAccepted = 0;
        uint8_t requestAttempts = 0, requestAccepted = 0;
        uint32_t ownEchoRx = 0, otherBusObs = 0, physicalInputRx = 0;
        uint32_t counterConflicts = 0, native3eeRx = 0;
        uint32_t consecutiveVehicleLocks = 0, autoRearms = 0;
        // beta09 P1: WaitDriverIntent native-RWD refusal watch (see
        // ApReRequestDiag). prevNativeStalk tracks the last native stalk
        // value for press-edge detection (0 after every idle frame).
        bool rwdPressPending = false, vehicleRefusal = false;
        uint8_t prevNativeStalk = 0;
        uint32_t rwdPressMs = 0, vehicleRefusals = 0;
        // beta09 P2: 0x399 high-nibble flag tally (all phases).
        uint8_t lastApFlag = 0;
        uint32_t lastApFlagMs = 0;
        uint32_t apFlagCounts[16] = {};
    } state_;
    mutable std::mutex mutex_;

    // User-frozen qualification set: DAS_autopilotState 2 or 3, membership
    // semantics (no 2->3 edge requirement).
    static bool qualificationApAllowed(uint8_t ap) { return ap == 2 || ap == 3; }
    static bool handshakeActive(Phase p)
    {
        return p == Phase::CancelSequence || p == Phase::WaitCancelEvidence ||
               p == Phase::RequestSequence || p == Phase::WaitQualification;
    }
    static bool roundActive(Phase p)
    {
        return handshakeActive(p) || p == Phase::ActiveInjection;
    }
    static const char *actionName(ApReRequestAction a)
    {
        switch (a)
        {
        case ApReRequestAction::CancelPress:
            return "cancelPress";
        case ApReRequestAction::CancelRelease:
            return "cancelRelease";
        case ApReRequestAction::RequestPress:
            return "requestPress";
        case ApReRequestAction::RequestRelease:
            return "requestRelease";
        default:
            return "none";
        }
    }

    bool dasFreshLocked(uint32_t now) const
    {
        return state_.lastDasMs != 0 && now - state_.lastDasMs <= kDasFreshMs &&
               state_.apState != 0xFF;
    }
    bool withinWindow(uint32_t now) const
    {
        return now - state_.requestMs < state_.profile.qualificationWindowMs;
    }

    // diag() has no injected clock; the window-remaining figure is computed
    // from the coordinator's own last-known timings only when meaningful.
    uint32_t diagNowDummyLocked() const { return state_.requestMs; }

    void startRound(uint32_t now)
    {
        ++state_.epoch;
        if (state_.epoch == 0) ++state_.epoch;
        ++state_.round;
        state_.roundStartMs = now;
        state_.exitSeen = false;
        // Barrier B: cancel evidence must be FRESH for this round. Leaving
        // the previous round's latch in place made any DAS observation of an
        // active state during the new round's WaitCancelEvidence (before the
        // fresh cancel landed — observed 22-526 ms) fire a FALSE
        // apActiveDuringWait via `stale seen && active`. The beta05 field
        // rounds dodged it only because the cancel landed faster than the
        // 0x399 cadence; the race was real (4.5.0-beta06 hardening).
        state_.cancelEvidenceSeen = false;
        state_.cancelEvidenceMs = 0;
        state_.cancelAttempts = state_.cancelAccepted = 0;
        state_.requestAttempts = state_.requestAccepted = 0;
        state_.awaitNativeAfterTx = false;
        state_.phase = Phase::CancelSequence;
        state_.step = Step::Idle;
        state_.reason = "cancelArmed";
    }

    void enterRequestSequence(uint32_t now)
    {
        state_.evidenceMs = now;
        state_.phase = Phase::RequestSequence;
        state_.step = Step::Idle;
        state_.reason = "requestArmed";
    }

    void completeRound(const char *reason)
    {
        ++state_.epoch;
        if (state_.epoch == 0) ++state_.epoch;
        state_.phase = Phase::Complete;
        state_.step = Step::Idle;
        state_.reason = reason;
        state_.lastEndedReason = reason;
        // A completed round proves the chain works end-to-end: the
        // consecutive vehicle-side lock budget starts fresh.
        state_.consecutiveVehicleLocks = 0;
    }

    // 4.5.0-beta06 fix 2: VEHICLE-SIDE lock reasons auto-re-arm instead of
    // locking. The beta05 Tier 1 field cost of the old behavior was that a
    // single vehicle hiccup (carrier timing, a rejected re-request, an 8/9
    // driver-abort cascade) froze the mode until the 8.3.6 switch was
    // manually cycled. Those events are transient vehicle conditions, not
    // device faults, and the re-arm is the same fence a normal Complete
    // round uses: a FULL AP exit must be observed (exitSeen) before the
    // next activation edge may start a new round, so a re-armed coordinator
    // never fires while the car is still in a weird state. Hardware-class
    // reasons (counterConflict / txFailed / permitLost / physicalInput /
    // invalidNative045 / dasStale and the round timeouts) still lock —
    // those indicate something the driver should look at, and locking is
    // the fail-closed answer. A cap (kVehicleAutoRearmMax consecutive
    // vehicle-side locks with no completed round between) degrades back to
    // a real FailedLocked so a persistently unhappy vehicle cannot loop
    // cancel/re-request forever.
    static bool vehicleSideRearmReason(const char *r)
    {
        return strcmp(r, "windowExpired") == 0 ||
               strcmp(r, "apExitedDuringWindow") == 0 ||
               strcmp(r, "apActiveDuringWait") == 0 ||
               strcmp(r, "apFaultState") == 0;
    }
    static constexpr uint32_t kVehicleAutoRearmMax = 3;

    void failLocked(const char *reason)
    {
        ++state_.epoch;
        if (state_.epoch == 0) ++state_.epoch;
        state_.step = Step::Idle;
        state_.lastEndedReason = reason;
        if (vehicleSideRearmReason(reason) &&
            state_.consecutiveVehicleLocks < kVehicleAutoRearmMax)
        {
            ++state_.consecutiveVehicleLocks;
            ++state_.autoRearms;
            state_.phase = Phase::WaitDriverIntent;
            state_.exitSeen = false;
            state_.reason = reason; // stays visible until the next state change
            return;
        }
        if (vehicleSideRearmReason(reason))
            state_.reason = "autoRearmLimit"; // cap exceeded: real lock
        else
            state_.reason = reason;
        state_.phase = Phase::FailedLocked;
    }

    void buildFrame(uint8_t out[8], uint8_t stalkValue)
    {
        memcpy(out, state_.templateData, 8);
        out[state_.profile.stalkByte] = static_cast<uint8_t>(
            (out[state_.profile.stalkByte] & ~state_.profile.stalkMask) |
            (stalkValue & state_.profile.stalkMask));
        // A fresh press must send "the counter the car itself is at, +1".
        // The 2026-09-11 Tier 1 CSVs taught this in two steps:
        // - beta03 collision: the native bus cloned our press inside the
        //   release gap, so the template lagged our own last TX when the
        //   request press built its frame 1 ms after the release; anchoring
        //   the template alone repeated the release counter (two frames
        //   1 ms apart, same counter — both failed rounds). Anchor our last
        //   TX when it is NEWER than the last template refresh: the native
        //   stream has not spoken since we did, so our counter is where the
        //   car is.
        // - beta04 stale-lastTx: the ring comparison had no time gating, so
        //   the previous round's release counter (83 s old in the CSV) still
        //   "led" the fresh template by 1 on the ring and the round-2 press
        //   skipped the counter the car was waiting for (device sent 7 while
        //   the car echoed 6) -> counterConflict lock, release never sent.
        //   Time separates stale from current; the ring cannot. When the
        //   template refresh is newer (the usual case), always anchor
        //   template+1: the car accepts its own current counter +1 (three
        //   round-1 echoes proved it: press, release, request press).
        // buildFrame only runs past the tick's template-freshness gate
        // (age <= kNative045MaxAgeMs), so the unsigned gap below is small
        // only when lastTx really is the newer event (wrap-safe compare).
        uint8_t base;
        if (state_.step == Step::Idle)
        {
            base = ap_rerequest_045::counter(state_.templateData);
            if (state_.lastTxValid &&
                static_cast<uint32_t>(state_.lastTxMs - state_.templateMs) <
                    kNative045MaxAgeMs)
                base = state_.lastTxCounter;
        }
        else
        {
            base = state_.lastTxCounter;
        }
        const uint8_t nextCounter = static_cast<uint8_t>((base + 1) & 0x0F);
        out[6] = static_cast<uint8_t>((out[6] & 0x0F) | (nextCounter << 4));
        out[7] = ap_rerequest_045::crc8J1850(out, 7);
        memcpy(state_.builtData, out, 8);
        state_.templateCounter = ap_rerequest_045::counter(state_.templateData);
    }
};
