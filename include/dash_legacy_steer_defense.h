#pragma once

#include <cstdint>
#include "dash_abort_guard.h" // kDashMinimalInjectBudget (single source of truth)

// v2.16-beta.16/.17/.19 Legacy steer-jerk defense (#108). One deep module
// owning engagement-edge detection + Instant Engage + Minimal Inject burst +
// settle fallback. Replaces the prior split (DashApFirstGate + base
// DashMinimalInject + dashboard minimalBypass). Base-class abortGuard/
// minimalInject remain for NAG/HW3/HW4; Legacy mux0 is the sole authority here.
//
// Seam: a LegacyHandler member consulted once per 0x3EE mux0 frame by the
// dashboard callback dashLegacyFsdActivationAllowed() (via decideLegacySteer),
// fed once per primary 921 frame by observe().

struct DashLegacySteerDecision
{
    bool engaged{false};           // AP in engaged state (3..6) this frame
    bool genuineEdge{false};       // a real rising edge was observed (pre-consume)
    bool debounceSatisfied{false}; // engaged && (now - engagedSince) >= debounceMs
    bool instantBypass{false};     // Instant fired this frame (consumed the edge)
    bool minimalBurstOpen{false};  // Minimal burst window open AND budget remains
    bool allowed{false};           // the activation verdict for THIS frame
};

struct DashLegacySteerDiag
{
    bool apEngaged{false};
    bool edgePending{false};
    bool instantEnabled{false};
    bool minimalEnabled{false};
    bool debounceSatisfied{false};
    uint32_t apEdgeCount{0}; // cumulative edges (survives resetRuntime)
    bool hasApEdge{false};
    uint32_t lastApEdgeMs{0};
    uint32_t instantBypassCount{0}; // cumulative Instant fires (survives resetRuntime)
    bool instantBypassLast{false};
    uint8_t minimalBudget{0}; // == kMinimalBudget
    uint8_t minimalUsed{0};
    uint32_t minimalBlocks{0};
    const char *lastMinimalBlockPath{"none"};
    const char *lastResetReason{"none"};
};

class DashLegacySteerDefense
{
public:
    static constexpr uint8_t kMinimalBudget = kDashMinimalInjectBudget; // 5, no drift

    // --- configuration (driven by toggles via dashApplyRuntimeState) ---
    void setInstantEnabled(bool e) { instantEnabled_ = e; }
    void setMinimalEnabled(bool e)
    {
        if (minimalEnabled_ == e)
            return;
        minimalEnabled_ = e;
        minimalWindowOpen_ = false;
        minimalUsed_ = 0;
        minimalBlocks_ = 0;
        lastMinimalBlockPath_ = "none";
        lastResetReason_ = e ? "enabled" : "disabled";
    }
    void setDebounceMs(uint32_t ms) { debounceMs_ = ms; }

    // --- edge observation: once per primary 921 frame ---
    void observe(uint8_t apState, uint32_t nowMs)
    {
        const bool engaged = isEngagedState(apState);
        if (!haveObservation_)
        {
            // Startup: record baseline, but a startup-engaged is NOT an edge.
            haveObservation_ = true;
            apEngaged_ = engaged;
            if (engaged)
            {
                haveEngagedSince_ = true;
                engagedSinceMs_ = nowMs;
            }
            return;
        }

        if (engaged != apEngaged_)
        {
            apEngaged_ = engaged;
            if (!engaged)
            {
                // Disengage / abort (8/9 are non-engaged): clear transient timing
                // and re-arm the Minimal burst budget for the next engagement.
                clearTiming();
                disarmMinimalBurst("disengage");
                return;
            }
            // Genuine rising edge into engagement.
            haveEngagedSince_ = true;
            engagedSinceMs_ = nowMs;
            edgePending_ = true;
            ++apEdgeCount_;
            hasApEdge_ = true;
            lastApEdgeMs_ = nowMs;
            // Arm the Minimal burst window only if Minimal is enabled AT the
            // edge — toggling Minimal on mid-episode (no fresh edge) must NOT
            // open the window (beta.17 edge semantics).
            if (minimalEnabled_)
            {
                minimalWindowOpen_ = true;
                lastResetReason_ = "edge";
            }
            return;
        }

        if (engaged && !haveEngagedSince_)
        {
            haveEngagedSince_ = true;
            engagedSinceMs_ = nowMs;
        }
    }

    // --- per-mux0-frame verdict (called exactly once per 0x3EE mux0 frame) ---
    DashLegacySteerDecision decide(uint32_t nowMs)
    {
        DashLegacySteerDecision d;
        d.engaged = apEngaged_;
        if (!apEngaged_)
        {
            instantBypassLast_ = false;
            return d;
        }

        d.genuineEdge = edgePending_;
        d.debounceSatisfied = haveEngagedSince_ && (nowMs - engagedSinceMs_) >= debounceMs_;

        if (minimalEnabled_)
        {
            // Minimal governs (beta.17): a 5-frame edge burst, then hard STOP.
            // The window is armed in observe() only when Minimal is enabled at
            // the engagement edge; toggling on mid-episode does NOT open it.
            d.minimalBurstOpen = minimalWindowOpen_ && minimalUsed_ < kMinimalBudget;
            d.allowed = d.minimalBurstOpen;
            if (edgePending_)
                edgePending_ = false;
            instantBypassLast_ = false; // Minimal governs; Instant did not fire
        }
        else
        {
            // Instant one-shot edge bypass of debounce (beta.16); otherwise settle.
            d.instantBypass = instantEnabled_ && edgePending_ && !d.debounceSatisfied;
            d.allowed = d.debounceSatisfied || d.instantBypass;
            if (d.instantBypass)
            {
                edgePending_ = false;
                ++instantBypassCount_;
            }
            instantBypassLast_ = d.instantBypass;
        }
        return d;
    }

    // --- budget accounting: call after a successful mux0 activation send ---
    bool recordMinimalInjection(const char *path)
    {
        if (!minimalEnabled_ || !minimalWindowOpen_)
            return true; // not driving -> no-op (no double-limiting)
        if (minimalUsed_ >= kMinimalBudget)
        {
            ++minimalBlocks_;
            lastMinimalBlockPath_ = path ? path : "unknown";
            return false;
        }
        ++minimalUsed_;
        if (minimalUsed_ == kMinimalBudget)
            lastResetReason_ = "exhausted";
        return true;
    }

    void recordMinimalBlock(const char *path)
    {
        ++minimalBlocks_;
        lastMinimalBlockPath_ = path ? path : "unknown";
    }

    // --- reset helpers (mirror DashApFirstGate names) ---
    void clearTiming()
    {
        haveEngagedSince_ = false;
        engagedSinceMs_ = 0;
        edgePending_ = false;
        instantBypassLast_ = false;
    }

    void resetRuntime()
    {
        haveObservation_ = false;
        apEngaged_ = false;
        disarmMinimalBurst("reset");
        clearTiming();
    }

    DashLegacySteerDiag diag(uint32_t nowMs) const
    {
        DashLegacySteerDiag d;
        d.apEngaged = apEngaged_;
        d.edgePending = edgePending_;
        d.instantEnabled = instantEnabled_;
        d.minimalEnabled = minimalEnabled_;
        d.debounceSatisfied = apEngaged_ && haveEngagedSince_ && (nowMs - engagedSinceMs_) >= debounceMs_;
        d.apEdgeCount = apEdgeCount_;
        d.hasApEdge = hasApEdge_;
        d.lastApEdgeMs = lastApEdgeMs_;
        d.instantBypassCount = instantBypassCount_;
        d.instantBypassLast = instantBypassLast_;
        d.minimalBudget = kMinimalBudget;
        d.minimalUsed = minimalUsed_;
        d.minimalBlocks = minimalBlocks_;
        d.lastMinimalBlockPath = lastMinimalBlockPath_;
        d.lastResetReason = lastResetReason_;
        return d;
    }

    static bool isEngagedState(uint8_t apState) { return apState >= 3 && apState <= 6; }

private:
    void disarmMinimalBurst(const char *reason)
    {
        if (minimalEnabled_ && minimalUsed_ != 0)
            lastResetReason_ = reason;
        minimalWindowOpen_ = false;
        minimalUsed_ = 0;
    }

    bool haveObservation_{false};
    bool apEngaged_{false};
    bool haveEngagedSince_{false};
    uint32_t engagedSinceMs_{0};
    bool edgePending_{false};

    bool instantEnabled_{false};
    bool minimalEnabled_{false};
    uint32_t debounceMs_{0};

    bool minimalWindowOpen_{false};
    uint8_t minimalUsed_{0};

    uint32_t apEdgeCount_{0};
    bool hasApEdge_{false};
    uint32_t lastApEdgeMs_{0};
    uint32_t instantBypassCount_{0};
    bool instantBypassLast_{false};
    uint32_t minimalBlocks_{0};
    const char *lastMinimalBlockPath_{"none"};
    const char *lastResetReason_{"none"};
};
