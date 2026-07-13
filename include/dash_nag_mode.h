#pragma once

#include <cstdint>

enum class DashNagMode : uint8_t
{
    Off = 0,
    HumanReplayTsl6p = 1,
    EpasLateEcho = 2,
    ReactiveHold = 3,
};

constexpr uint8_t dashNagModeToRaw(DashNagMode mode)
{
    return static_cast<uint8_t>(mode);
}

constexpr bool dashNagModeIsValid(uint8_t raw)
{
    return raw <= dashNagModeToRaw(DashNagMode::ReactiveHold);
}

constexpr DashNagMode dashNagModeFromRaw(uint8_t raw)
{
    return dashNagModeIsValid(raw)
               ? static_cast<DashNagMode>(raw)
               : DashNagMode::Off;
}

constexpr const char *dashNagModeName(DashNagMode mode)
{
    switch (mode)
    {
    case DashNagMode::HumanReplayTsl6p:
        return "human_replay_tsl6p";
    case DashNagMode::EpasLateEcho:
        return "late_echo";
    case DashNagMode::ReactiveHold:
        return "reactive_hold";
    case DashNagMode::Off:
    default:
        return "off";
    }
}

inline bool dashTryParseNagMode(const char *raw, DashNagMode &out)
{
    if (!raw || raw[0] < '0' || raw[0] > '3' || raw[1] != '\0')
        return false;

    out = dashNagModeFromRaw(static_cast<uint8_t>(raw[0] - '0'));
    return true;
}
