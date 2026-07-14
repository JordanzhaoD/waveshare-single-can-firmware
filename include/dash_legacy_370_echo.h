#pragma once

#include "can_helpers.h"

inline CanFrame dashBuildLegacy370Echo(
    const CanFrame &source,
    uint8_t torqueData2LowNibble,
    uint8_t torqueData3,
    bool forceHandsOnLevel1)
{
    CanFrame out = source;
    out.id = 0x370;
    out.dlc = 8;
    out.data[2] = static_cast<uint8_t>(
        (source.data[2] & 0xF0) | (torqueData2LowNibble & 0x0F));
    out.data[3] = torqueData3;
    out.data[4] = forceHandsOnLevel1
                      ? static_cast<uint8_t>((source.data[4] & 0x3F) | 0x40)
                      : source.data[4];
    out.data[6] = static_cast<uint8_t>(
        (source.data[6] & 0xF0) | ((source.data[6] + 1) & 0x0F));
    out.data[7] = computeVehicleChecksum(out);
    return out;
}
