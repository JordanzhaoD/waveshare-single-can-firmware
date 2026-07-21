#pragma once

#include <cstdint>

enum class DashNagRouteBlock : uint8_t
{
    None = 0,
    ModeOff,
    CanWriteOff,
    Ota,
    ApGate,
    AbortGuard,
};

inline DashNagRouteBlock dashNagRouteBlock(bool modeEnabled, bool canWriteEnabled,
                                           bool otaAllowed, bool apGateAllowed,
                                           bool abortGuardAllowed)
{
    if (!modeEnabled)
        return DashNagRouteBlock::ModeOff;
    if (!canWriteEnabled)
        return DashNagRouteBlock::CanWriteOff;
    if (!otaAllowed)
        return DashNagRouteBlock::Ota;
    if (!apGateAllowed)
        return DashNagRouteBlock::ApGate;
    if (!abortGuardAllowed)
        return DashNagRouteBlock::AbortGuard;
    return DashNagRouteBlock::None;
}

inline const char *dashNagRouteBlockName(DashNagRouteBlock reason)
{
    switch (reason)
    {
    case DashNagRouteBlock::None:
        return "none";
    case DashNagRouteBlock::ModeOff:
        return "modeOff";
    case DashNagRouteBlock::CanWriteOff:
        return "canWriteOff";
    case DashNagRouteBlock::Ota:
        return "ota";
    case DashNagRouteBlock::ApGate:
        return "apGate";
    case DashNagRouteBlock::AbortGuard:
        return "abortGuard";
    }
    return "unknown";
}

inline uint8_t dashMergeNagFilterIds(const uint32_t *handlerIds, uint8_t handlerCount,
                                     const uint32_t *nagIds, uint8_t nagCount,
                                     uint32_t *out, uint8_t maxOut)
{
    if (!out || maxOut == 0)
        return 0;

    uint8_t count = 0;
    const auto appendUnique = [&](uint32_t id) {
        for (uint8_t i = 0; i < count; i++)
        {
            if (out[i] == id)
                return;
        }
        if (count < maxOut)
            out[count++] = id;
    };

    if (handlerIds)
    {
        for (uint8_t i = 0; i < handlerCount; i++)
            appendUnique(handlerIds[i]);
    }
    if (nagIds)
    {
        for (uint8_t i = 0; i < nagCount; i++)
            appendUnique(nagIds[i]);
    }
    return count;
}
