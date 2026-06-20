#pragma once

#include <cstdint>

static inline const char *dashCapabilityLegacyFsd(uint8_t effectiveHw)
{
    return effectiveHw == 0 ? "supported" : "supported";
}

static inline const char *dashCapabilityLegacySpeedOffset(uint8_t effectiveHw)
{
    return effectiveHw == 0 ? "supported" : "unsupported_on_legacy";
}

static inline const char *dashCapabilityBionicSteering(uint8_t effectiveHw)
{
    return effectiveHw == 0 ? "unsupported_on_legacy" : "experimental";
}

static inline const char *dashCapabilityBanShield(uint8_t effectiveHw)
{
    return effectiveHw == 2 ? "supported" : "unsupported_on_legacy";
}

static inline const char *dashCapabilityLegacyMppCustom(uint8_t effectiveHw)
{
    return effectiveHw == 0 ? "not_wired" : "unsupported_on_legacy";
}
