#pragma once

#include <cstdint>

enum class DashTwaiState : uint8_t
{
    Unknown = 0,
    Running = 1,
    BusOff = 2,
    Recovering = 3,
    Stopped = 4,
};

static inline const char *dashTwaiStateName(DashTwaiState state)
{
    switch (state)
    {
    case DashTwaiState::Running:
        return "running";
    case DashTwaiState::BusOff:
        return "bus_off";
    case DashTwaiState::Recovering:
        return "recovering";
    case DashTwaiState::Stopped:
        return "stopped";
    case DashTwaiState::Unknown:
        return "unknown";
    }
    return "unknown";
}

struct DashTwaiDiag
{
    DashTwaiState state = DashTwaiState::Unknown;
    uint32_t txErrorCounter = 0;
    uint32_t rxErrorCounter = 0;
    uint32_t msgsToTx = 0;
    uint32_t msgsToRx = 0;
    uint32_t txFailed = 0;
    uint32_t rxMissed = 0;
    uint32_t rxOverrun = 0;
    uint32_t arbLost = 0;
    uint32_t busError = 0;
    uint32_t recoveries = 0;
    uint32_t lastRecoveryMs = 0;
    uint32_t framesRead = 0;
    uint32_t framesAccepted = 0;
    uint32_t framesDropped = 0;
    uint32_t readDrainBudgetHits = 0;
};

static inline uint32_t dashAgeMs(uint32_t nowMs, uint32_t eventMs)
{
    // eventMs == 0 means "never observed" in the dashboard diagnostics. Returning
    // UINT32_MAX made callers that render seconds show values around 4294964s.
    // Use a safe numeric age of 0 for missing events and preserve unsigned delta
    // behavior for real timestamps.
    return eventMs == 0 ? 0 : nowMs - eventMs;
}
