#pragma once

#include <cstdint>

struct DashApFirstDecision
{
    bool engaged{false};
    bool edgeDetected{false};
    bool debounceSatisfied{false};
    bool instantBypass{false};
    bool allowed{false};
};

struct DashApFirstDiag
{
    bool apEngaged{false};
    bool edgePending{false};
    bool debounceSatisfied{false};
    uint32_t apEdgeCount{0};
    bool hasApEdge{false};
    uint32_t lastApEdgeMs{0};
    uint32_t apDebounceBypassCount{0};
};

class DashApFirstGate
{
public:
    void observe(uint8_t apState, uint32_t nowMs)
    {
        const bool engaged = isEngagedState(apState);
        if (!haveObservation_)
        {
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
                clearTiming();
                return;
            }

            haveEngagedSince_ = true;
            engagedSinceMs_ = nowMs;
            edgePending_ = true;
            apEdgeCount_++;
            hasApEdge_ = true;
            lastApEdgeMs_ = nowMs;
            return;
        }

        if (engaged && !haveEngagedSince_)
        {
            haveEngagedSince_ = true;
            engagedSinceMs_ = nowMs;
        }
    }

    DashApFirstDecision decide(bool apGateEnabled,
                               bool instantEngageEnabled,
                               uint32_t debounceMs,
                               uint32_t nowMs)
    {
        lastDebounceMs_ = debounceMs;
        haveDebounceConfig_ = true;

        DashApFirstDecision decision;
        decision.engaged = apEngaged_;

        if (!apGateEnabled)
        {
            clearTiming();
            decision.allowed = true;
            return decision;
        }

        decision.edgeDetected = edgePending_;
        decision.debounceSatisfied =
            decision.engaged &&
            haveEngagedSince_ &&
            (nowMs - engagedSinceMs_) >= debounceMs;
        decision.instantBypass =
            decision.engaged &&
            edgePending_ &&
            instantEngageEnabled &&
            !decision.debounceSatisfied;
        decision.allowed =
            decision.engaged &&
            (decision.debounceSatisfied || decision.instantBypass);

        if (decision.instantBypass && decision.allowed)
        {
            edgePending_ = false;
            apDebounceBypassCount_++;
        }

        return decision;
    }

    void clearTiming()
    {
        haveEngagedSince_ = false;
        engagedSinceMs_ = 0;
        edgePending_ = false;
    }

    void resetRuntime()
    {
        haveObservation_ = false;
        apEngaged_ = false;
        haveDebounceConfig_ = false;
        lastDebounceMs_ = 0;
        clearTiming();
    }

    DashApFirstDiag diag(uint32_t nowMs) const
    {
        DashApFirstDiag result;
        result.apEngaged = apEngaged_;
        result.edgePending = edgePending_;
        result.debounceSatisfied =
            apEngaged_ &&
            haveEngagedSince_ &&
            haveDebounceConfig_ &&
            (nowMs - engagedSinceMs_) >= lastDebounceMs_;
        result.apEdgeCount = apEdgeCount_;
        result.hasApEdge = hasApEdge_;
        result.lastApEdgeMs = lastApEdgeMs_;
        result.apDebounceBypassCount = apDebounceBypassCount_;
        return result;
    }

    static bool isEngagedState(uint8_t apState)
    {
        return apState >= 3 && apState <= 6;
    }

private:
    bool haveObservation_{false};
    bool apEngaged_{false};
    bool haveEngagedSince_{false};
    uint32_t engagedSinceMs_{0};
    bool edgePending_{false};

    bool haveDebounceConfig_{false};
    uint32_t lastDebounceMs_{0};

    uint32_t apEdgeCount_{0};
    bool hasApEdge_{false};
    uint32_t lastApEdgeMs_{0};
    uint32_t apDebounceBypassCount_{0};
};
