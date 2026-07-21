#pragma once

#include <cstdint>

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
static constexpr uint8_t kDashMinimalInjectBudget = 5;

struct DashMinimalInjectDiag
{
    bool enabled = false;
    bool apEngaged = false;
    uint8_t budget = kDashMinimalInjectBudget;
    uint8_t used = 0;
    uint32_t blocks = 0;
    const char *lastBlockedPath = "none";
    const char *lastResetReason = "none";
};

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

        if (apState == 8 || apState == 9)
        {
            if (!latched_)
                latchedAtMs_ = nowMs;
            latched_ = true;
            lastAbortState_ = apState;
            lastClearReason_ = "none";
            return;
        }

        // AP state 2 is AVAILABLE, not a valid engagement. Re-arm on every
        // non-engaged state, matching upstream's DAS_APSTATE_ENGAGED == 3.
        if (latched_ && apState < 3)
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

class DashMinimalInject
{
public:
    void setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        used_ = 0;
        blocks_ = 0;
        lastBlockedPath_ = "none";
        lastResetReason_ = enabled ? "enabled" : "disabled";
    }

    void onApState(uint8_t apState)
    {
        apEngaged_ = apState >= 3 && apState <= 6;
        if (!enabled_ || apEngaged_)
            return;

        // A disengage or abort starts a fresh engagement budget. Abort Guard
        // remains responsible for blocking the abort itself.
        if (used_ != 0)
            lastResetReason_ = "disengage";
        used_ = 0;
    }

    bool allowsInjection() const
    {
        return !enabled_ || used_ < kDashMinimalInjectBudget;
    }

    bool recordInjection()
    {
        if (!allowsInjection())
            return false;
        if (enabled_)
            ++used_;
        return true;
    }

    void recordBlock(const char *path)
    {
        if (allowsInjection())
            return;
        ++blocks_;
        lastBlockedPath_ = path ? path : "unknown";
    }

    DashMinimalInjectDiag diag() const
    {
        DashMinimalInjectDiag d;
        d.enabled = enabled_;
        d.apEngaged = apEngaged_;
        d.used = used_;
        d.blocks = blocks_;
        d.lastBlockedPath = lastBlockedPath_;
        d.lastResetReason = lastResetReason_;
        return d;
    }

private:
    bool enabled_ = false;
    bool apEngaged_ = false;
    uint8_t used_ = 0;
    uint32_t blocks_ = 0;
    const char *lastBlockedPath_ = "none";
    const char *lastResetReason_ = "none";
};
