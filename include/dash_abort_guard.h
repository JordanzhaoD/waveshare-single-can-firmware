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
