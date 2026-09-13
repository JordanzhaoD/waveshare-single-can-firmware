#pragma once

#include <cstdint>

// Abort-Guard abort states — upstream v2.16-beta.11 `fsd_handler.h`
// (`DAS_APSTATE_ABORTING` / `DAS_APSTATE_ABORTED`): DAS_autopilotState values
// that mean the car is aborting an engage, the moment linked to the steer-jerk.
// Kept as named constants to mirror upstream exactly.
static constexpr uint8_t kDasApStateAborting = 8u;
static constexpr uint8_t kDasApStateAborted = 9u;

enum class DashAbortGuardBlockPath : uint8_t
{
    None = 0,
    LegacyFsdMux0,
    LegacyFsdMux1,
    LegacySpeed0x2f8,
    LegacyVisionSlider0x438,
    Nag,
    ApAutoRestore,
    Hw3DasStatus921,
    Hw3FsdMux0,
    Hw3FsdMux1,
    Hw3FsdMux2,
    Hw4DasStatus921,
    Hw4DasStatus923,
    Hw4FsdMux0,
    Hw4FsdMux1,
    Hw4FsdMux2,
};

struct DashAbortGuardDiag
{
    bool enabled = false;
    bool latched = false;
    uint8_t lastApState = 0;
    uint8_t lastAbortState = 0;
    uint32_t latchedAtMs = 0;
    const char *lastClearReason = "none";
    uint32_t blocks = 0;
    const char *lastBlockedPath = "none";
};

// v2.16-beta.19 parity: when enabled, send only a short AP-enable burst at
// the real engagement edge. This is separate from Abort Guard's abort latch:
// it keeps optional activation injection away from the later 6 -> 8/9 window.
// (The DashMinimalInject family — kDashMinimalInjectBudget, DashMinimalInjectDiag,
// the class below — was removed in v1.18 with the #108 steer-jerk defense
// (dual-CAN 4.5.0-beta02 precedent); Abort Guard itself is kept: it is the
// 0x399 state 8/9 latch safety guard and the coordinator's injection path
// goes through it.)

inline const char *dashAbortGuardBlockPathName(DashAbortGuardBlockPath path)
{
    switch (path)
    {
    case DashAbortGuardBlockPath::LegacyFsdMux0:
        return "legacy_fsd_mux0";
    case DashAbortGuardBlockPath::LegacyFsdMux1:
        return "legacy_fsd_mux1";
    case DashAbortGuardBlockPath::LegacySpeed0x2f8:
        return "legacy_speed_0x2f8";
    case DashAbortGuardBlockPath::LegacyVisionSlider0x438:
        return "legacy_vision_slider_0x438";
    case DashAbortGuardBlockPath::Nag:
        return "nag";
    case DashAbortGuardBlockPath::ApAutoRestore:
        return "ap_auto_restore";
    case DashAbortGuardBlockPath::Hw3DasStatus921:
        return "hw3_das_status_921";
    case DashAbortGuardBlockPath::Hw3FsdMux0:
        return "hw3_fsd_mux0";
    case DashAbortGuardBlockPath::Hw3FsdMux1:
        return "hw3_fsd_mux1";
    case DashAbortGuardBlockPath::Hw3FsdMux2:
        return "hw3_fsd_mux2";
    case DashAbortGuardBlockPath::Hw4DasStatus921:
        return "hw4_das_status_921";
    case DashAbortGuardBlockPath::Hw4DasStatus923:
        return "hw4_das_status_923";
    case DashAbortGuardBlockPath::Hw4FsdMux0:
        return "hw4_fsd_mux0";
    case DashAbortGuardBlockPath::Hw4FsdMux1:
        return "hw4_fsd_mux1";
    case DashAbortGuardBlockPath::Hw4FsdMux2:
        return "hw4_fsd_mux2";
    case DashAbortGuardBlockPath::None:
    default:
        return "none";
    }
}

class DashAbortGuard
{
public:
    void setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        if (!enabled_)
        {
            latched_ = false;
            lastClearReason_ = "disabled";
        }
    }

    void onApState(uint8_t apState, uint32_t nowMs)
    {
        lastApState_ = apState;
        if (!enabled_)
            return;

        if (apState == kDasApStateAborting || apState == kDasApStateAborted)
        {
            if (!latched_)
                latchedAtMs_ = nowMs;
            latched_ = true;
            lastAbortState_ = apState;
            lastClearReason_ = "none";
            return;
        }

        // Upstream v2.16-beta.11 `fsd_abort_guard_update`: a clean disengage is
        // DAS_autopilotState < 2 (UNAVAIL=0 / AVAIL=1). State 2 is
        // ACTIVE_NOMINAL — still an engaged-ish state — so it does NOT re-arm
        // the latch; only a true disengage (< 2) does. This keeps the guard
        // suppressing injection longer than a "< 3" threshold would, matching
        // the upstream probe that tested best on the jerk-prone road.
        if (latched_ && apState < 2)
        {
            latched_ = false;
            lastClearReason_ = "cleanDisengage";
        }
    }

    bool allowsInjection() const
    {
        return !enabled_ || !latched_;
    }

    void recordBlock(DashAbortGuardBlockPath path)
    {
        if (allowsInjection())
            return;
        ++blocks_;
        lastBlockedPath_ = dashAbortGuardBlockPathName(path);
    }

    DashAbortGuardDiag diag() const
    {
        DashAbortGuardDiag d;
        d.enabled = enabled_;
        d.latched = latched_;
        d.lastApState = lastApState_;
        d.lastAbortState = lastAbortState_;
        d.latchedAtMs = latchedAtMs_;
        d.lastClearReason = lastClearReason_;
        d.blocks = blocks_;
        d.lastBlockedPath = lastBlockedPath_;
        return d;
    }

private:
    bool enabled_ = false;
    bool latched_ = false;
    uint8_t lastApState_ = 0;
    uint8_t lastAbortState_ = 0;
    uint32_t latchedAtMs_ = 0;
    const char *lastClearReason_ = "none";
    uint32_t blocks_ = 0;
    const char *lastBlockedPath_ = "none";
};


// (DashMinimalInject class removed in v1.18 with the #108 steer-jerk
// defense family — see the note above dashAbortGuardBlockPathName.)
