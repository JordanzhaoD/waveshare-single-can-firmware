#pragma once

#include <cstdint>
#include <cstring>

#include "can_frame_types.h"

// ── 0x045 frame helpers ─────────────────────────────────────────────────
// Kept from the v1.18 coordinator family (inlined from the dual-CAN
// dash_legacy_wheel_045_dnd.h, verified against 3202/3202 real-vehicle
// frames): CRC8-J1850 over bytes 0..6 with the 4-bit rolling counter in
// byte6's HIGH nibble. LittleGong's table decodes as a different init/xorout
// — we keep OUR field-validated variant on purpose (do not blindly copy).
namespace jitter_045
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

inline uint8_t counterOf(const uint8_t data[8])
{
    return static_cast<uint8_t>((data[6] >> 4) & 0x0F);
}
} // namespace jitter_045

// v1.19 "JITTER" 8.3.6 procedure — a from-disassembly port of the
// LittleGong (ColorBlack_T-1.3.8) anti-jerk state machine that Jordan
// reported as field-stable on the same single-CAN wiring. It REPLACES the
// v1.18 coordinator family (dash_ap_rerequest_activation.h, 1079 lines):
// no failure taxonomy, no retries, no auto-re-arm accounting — a sequence
// that does not complete simply stops, and the human retries.
//
// Decoded LittleGong machine (event source = 0x399 data[0]&0x0F):
//
//   state0 ── any nonzero event ──> state1 (normal monitoring)
//   state1 ── event 3 (AP engage) ──> ARMING: deadline = now + 5000 ms,
//           one-shot 0x3EE bit46 patched clone sent immediately
//           (no native template cached -> skipped, cycle continues)
//   ARMING ── event 6 (FSD lane capture) ── gate (once per period) ──>
//           DISENGAGING + 0x045 cancel burst (0x41)
//   ARMING ── 5000 ms timeout ──> full reset
//   DISENGAGING ── event 1 (AP dropped) ── note "cancel seen"
//   DISENGAGING ── event 2 (AP available) ── cycle count + 0x045
//           re-request burst (0x42) + deadline refresh
//   any ── events 8/9/10 (AP fault) ── full reset
//
// LittleGong constants (all instruction-level decoded): 5000 ms arming /
// refresh deadline, 47 ms native-0x045 carrier-lock window, 3 ms pump frame
// spacing, 16-frame burst cap, 45° / 90° steering abort thresholds. The one
// decoded constant deliberately NOT ported is the "config float > 3.0"
// event-6 gate: the compared quantity is unidentified, and binding it to a
// guessed speed source could silently dead-switch the procedure (event 6
// itself only occurs while driving, which is when the jerk risk exists).
//
// 0x488 abort (same lineage as our AbortGuard): while the procedure runs,
// DAS_trackAngle = ((b0<<8|b1)&0x7FFF − 16384) × 0.1° is parsed; >45° aborts
// the current burst, >90° resets the procedure. Our beta08 field data adds
// a LittleGong-invisible guard: the angle field is only meaningful when
// steeringControlType (b2 high 3 bits) == 2, so other types are ignored
// instead of tripping the abort on garbage values.
enum class JitterPhase : uint8_t
{
    Inert,       // switch off / AP gate closed / permit lost
    Idle,        // state0 equivalent: waiting for the first 0x399 event
    Normal,      // state1: monitoring for the AP engage edge
    Arming,      // state2: one-shot sent, waiting for lane capture
    Disengaging, // state4: cancel burst / wait available / re-request burst
};

enum class JitterAction : uint8_t
{
    None = 0,
    Stalk045,  // outData[8] carries a synthetic 0x045 frame (CRC included)
    Bit46Shot, // outData[8] carries a patched native 0x3EE mux0 clone
};

struct DashJitterDiag
{
    bool requested = false; // persisted switch
    bool effective = false; // switch AND gate AND permit
    bool apGateOpen = false;
    bool permit = false;
    JitterPhase phase = JitterPhase::Inert;
    const char *reason = "off";
    uint8_t apState = 0;     // last observed 0x399 low nibble
    uint8_t cycles = 0;      // cancel/re-request cycles this period
    uint32_t cancels = 0;    // cancel bursts started
    uint32_t requests = 0;   // re-request bursts started
    uint32_t bit46Shots = 0; // one-shot 0x3EE frames emitted
    uint32_t pumpFrames = 0; // synthetic 0x045 frames emitted
    uint32_t txOk = 0;
    uint32_t txFail = 0;
    uint32_t steerAborts = 0;   // >45° burst aborts
    uint32_t steerResets = 0;   // >90° full resets
    uint32_t apErrorResets = 0; // events 8/9/10 resets
    uint32_t timeoutResets = 0; // deadline expiries
};

struct DashJitterProcedure
{
    // ── LittleGong field constants (disassembly-proven) ─────────────────
    static constexpr uint32_t kDeadlineMs = 5000;  // arming / refresh window
    static constexpr uint32_t kCarrierLockMs = 47; // native 0x045 freshness gate
    static constexpr uint32_t kPumpGapMs = 3;      // inter-frame spacing
    static constexpr uint8_t kPumpMaxFrames = 16;  // burst cap
    static constexpr float kSteerAbortDeg = 45.0f; // stop the burst
    static constexpr float kSteerResetDeg = 90.0f; // full reset
    // 0x3EE one-shot template freshness (fail-closed: no fresh template ->
    // skip the shot; 1 Hz mux0 carrier makes ~1 s the normal age).
    static constexpr uint32_t kNative3eeMaxAgeMs = 5000;

    // ── configuration ───────────────────────────────────────────────────
    bool enabled_ = false;
    bool gateOpen_ = false;
    bool permit_ = false;

    // ── phase machine ───────────────────────────────────────────────────
    JitterPhase phase_ = JitterPhase::Inert;
    const char *reason_ = "off";
    uint8_t apState_ = 0;
    uint8_t cycles_ = 0;
    uint32_t deadlineMs_ = 0;

    // ── native caches ───────────────────────────────────────────────────
    uint8_t native045_[8] = {};
    uint32_t native045Ms_ = 0;
    bool native045Seen_ = false;
    uint8_t native3ee_[8] = {};
    uint32_t native3eeMs_ = 0;
    bool native3eeSeen_ = false;

    // ── 0x045 carrier-locked pump ───────────────────────────────────────
    bool pumpActive_ = false;
    uint8_t pumpGesture_ = 0; // 1 = FWD cancel (0x41), 2 = RWD request (0x42)
    uint8_t pumpSent_ = 0;
    uint32_t pumpLastTxMs_ = 0;
    uint8_t pumpNextCounter_ = 0;

    // ── pending one-shot ────────────────────────────────────────────────
    bool bit46Queued_ = false;

    // ── 0x488 steering angle ────────────────────────────────────────────
    float steerAngleDeg_ = 0.0f;
    bool steerValid_ = false;

    // ── diagnostics counters ────────────────────────────────────────────
    uint32_t cancels_ = 0;
    uint32_t requests_ = 0;
    uint32_t bit46Shots_ = 0;
    uint32_t pumpFrames_ = 0;
    uint32_t txOk_ = 0;
    uint32_t txFail_ = 0;
    uint32_t steerAborts_ = 0;
    uint32_t steerResets_ = 0;
    uint32_t apErrorResets_ = 0;
    uint32_t timeoutResets_ = 0;

    // Armed = switch on AND gate open. The base bit46 path is only paused
    // while a procedure actually runs (procedureActive()); Idle/Normal leave
    // the base path untouched.
    void configure(bool enabled, bool apGateOpen)
    {
        enabled_ = enabled;
        gateOpen_ = enabled && apGateOpen;
        // Reason precision: switch off reports "off" even with the gate
        // closed too; gate closed alone reports "apGateOff". Both land in
        // Inert (armed() is false either way).
        if (!enabled_)
            fullReset("off");
        else if (!gateOpen_)
            fullReset("apGateOff");
        else if (phase_ == JitterPhase::Inert)
        {
            phase_ = JitterPhase::Idle;
            reason_ = "idle";
        }
    }

    bool armed() const { return enabled_ && gateOpen_; }
    bool procedureActive() const
    {
        return phase_ == JitterPhase::Arming || phase_ == JitterPhase::Disengaging;
    }

    // Permit loss mid-procedure is fail-closed like everything else here:
    // a plain reset (no lock taxonomy — the human retries), matching the
    // LittleGong "no failure handling" philosophy.
    void setPermit(bool ok)
    {
        permit_ = ok;
        if (!ok && procedureActive())
            fullReset("permitLost");
    }

    void fullReset(const char *reason)
    {
        // Back to state0-equivalent (Idle re-enters Normal on the next
        // nonzero 0x399 event); the triggering reason always wins for diag.
        phase_ = armed() ? JitterPhase::Idle : JitterPhase::Inert;
        reason_ = reason;
        cycles_ = 0;
        pumpActive_ = false;
        bit46Queued_ = false;
        deadlineMs_ = 0;
    }

    // ── observations (all timestamps caller-supplied; pure module) ──────
    void observeNative045(const uint8_t data[8], uint32_t nowMs)
    {
        memcpy(native045_, data, 8);
        native045Ms_ = nowMs;
        native045Seen_ = true;
    }

    void observeNative3eeMux0(const uint8_t data[8], uint32_t nowMs)
    {
        memcpy(native3ee_, data, 8);
        native3eeMs_ = nowMs;
        native3eeSeen_ = true;
    }

    void observeSteerAngle(float angleDeg, bool valid, uint32_t)
    {
        steerAngleDeg_ = angleDeg;
        steerValid_ = valid;
        if (!procedureActive() || !valid)
            return;
        const float a = angleDeg < 0 ? -angleDeg : angleDeg;
        if (a > kSteerResetDeg)
        {
            ++steerResets_;
            fullReset("steerReset");
        }
        else if (a > kSteerAbortDeg && pumpActive_)
        {
            ++steerAborts_;
            pumpActive_ = false; // abort the burst; the phase machine walks on
            reason_ = "steerAbort";
        }
    }

    void observeDasStatus(uint8_t apState, uint32_t nowMs)
    {
        apState_ = apState;
        if (!armed() || phase_ == JitterPhase::Inert)
            return;
        switch (phase_)
        {
        case JitterPhase::Idle:
            // state0 -> any nonzero event -> state1.
            if (apState != 0)
            {
                phase_ = JitterPhase::Normal;
                reason_ = "monitoring";
            }
            break;
        case JitterPhase::Normal:
            if (apState == 3 && permit_)
            {
                phase_ = JitterPhase::Arming;
                deadlineMs_ = nowMs + kDeadlineMs;
                bit46Queued_ = true;
                reason_ = "arming";
            }
            break;
        case JitterPhase::Arming:
            if (apState == 6 && cycles_ < 1 && permit_)
            {
                // "Recognize risk source, DISENGAGING" — the cancel lands at
                // the lane-capture entry, ~200 ms ahead of the beta08 jerk
                // window (state6 + 200..330 ms).
                phase_ = JitterPhase::Disengaging;
                deadlineMs_ = nowMs + kDeadlineMs;
                startPump(1, nowMs);
                ++cancels_;
                reason_ = "cancel";
            }
            else if (apState >= 8 && apState <= 10)
            {
                ++apErrorResets_;
                fullReset("apError");
            }
            break;
        case JitterPhase::Disengaging:
            if (apState == 1)
            {
                // LittleGong logs "Rengaged" here: the cancel took effect.
                reason_ = "cancelSeen";
            }
            else if (apState == 2 && permit_)
            {
                ++cycles_;
                startPump(2, nowMs);
                deadlineMs_ = nowMs + kDeadlineMs;
                ++requests_;
                reason_ = "reRequest";
            }
            else if (apState >= 8 && apState <= 10)
            {
                ++apErrorResets_;
                fullReset("apError");
            }
            break;
        default:
            break;
        }
    }

    void startPump(uint8_t gesture, uint32_t nowMs)
    {
        pumpActive_ = true;
        pumpGesture_ = gesture;
        pumpSent_ = 0;
        pumpLastTxMs_ = 0; // the carrier-lock gate spaces the first frame
        pumpNextCounter_ = native045Seen_
                               ? static_cast<uint8_t>((jitter_045::counterOf(native045_) + 1) & 0x0F)
                               : 0;
    }

    // ── tick ────────────────────────────────────────────────────────────
    // Returns the next frame the caller should transmit (see JitterAction).
    JitterAction tick(uint32_t nowMs, uint8_t outData[8])
    {
        if (!armed() || phase_ == JitterPhase::Inert)
            return JitterAction::None;

        // Deadline expiry: arming that never saw lane capture, or a
        // disengage cycle that ran out of road. Silent full reset.
        if (procedureActive() && deadlineMs_ != 0 &&
            static_cast<int32_t>(nowMs - deadlineMs_) >= 0)
        {
            ++timeoutResets_;
            fullReset("timeout");
        }

        // One-shot bit46 at the ARMING entry: patched clone of the freshest
        // native 0x3EE mux0 template (byte5 |= 0x43 forces the unlock bit
        // pattern LittleGong uses; counter/checksum ride the clone).
        if (bit46Queued_)
        {
            bit46Queued_ = false;
            if (native3eeSeen_ &&
                static_cast<uint32_t>(nowMs - native3eeMs_) <= kNative3eeMaxAgeMs)
            {
                memcpy(outData, native3ee_, 8);
                outData[5] = static_cast<uint8_t>(outData[5] | 0x43);
                ++bit46Shots_;
                return JitterAction::Bit46Shot;
            }
            // No fresh template: skip the shot (fail-closed), cycle continues.
        }

        // Carrier-locked 0x045 pump: only transmit riding right behind a
        // fresh native frame (<47 ms old), >=3 ms apart, <=16 per burst.
        if (pumpActive_)
        {
            const bool carrierFresh =
                native045Seen_ &&
                static_cast<uint32_t>(nowMs - native045Ms_) < kCarrierLockMs;
            if (carrierFresh && pumpSent_ < kPumpMaxFrames &&
                (pumpLastTxMs_ == 0 ||
                 static_cast<uint32_t>(nowMs - pumpLastTxMs_) >= kPumpGapMs))
            {
                outData[0] = static_cast<uint8_t>(0x40 | pumpGesture_);
                memcpy(outData + 1, native045_ + 1, 5);
                outData[6] = static_cast<uint8_t>((pumpNextCounter_ << 4) |
                                                  (native045_[6] & 0x0F));
                outData[7] = jitter_045::crc8J1850(outData, 7);
                pumpNextCounter_ =
                    static_cast<uint8_t>((pumpNextCounter_ + 1) & 0x0F);
                pumpLastTxMs_ = nowMs;
                ++pumpSent_;
                ++pumpFrames_;
                if (pumpSent_ >= kPumpMaxFrames)
                    pumpActive_ = false; // burst budget spent — silent stop
                return JitterAction::Stalk045;
            }
        }
        return JitterAction::None;
    }

    // LittleGong's answer to a failed send: silently stop the sequence. The
    // phase machine keeps walking (a dead cancel eventually times out).
    void recordTxResult(bool ok)
    {
        if (ok)
            ++txOk_;
        else
        {
            ++txFail_;
            pumpActive_ = false;
        }
    }

    DashJitterDiag diag() const
    {
        DashJitterDiag d;
        d.requested = enabled_;
        d.apGateOpen = gateOpen_;
        d.permit = permit_;
        d.effective = armed() && permit_;
        d.phase = phase_;
        d.reason = reason_;
        d.apState = apState_;
        d.cycles = cycles_;
        d.cancels = cancels_;
        d.requests = requests_;
        d.bit46Shots = bit46Shots_;
        d.pumpFrames = pumpFrames_;
        d.txOk = txOk_;
        d.txFail = txFail_;
        d.steerAborts = steerAborts_;
        d.steerResets = steerResets_;
        d.apErrorResets = apErrorResets_;
        d.timeoutResets = timeoutResets_;
        return d;
    }
};
