#pragma once
// Legacy (HW2.x) MPP speed-limit override — mirrors the HW3 custom-speed
// bucket-table UX, but writes UI_mppSpeedLimit on CAN ID 760 (UI_gpsVehicleSpeed)
// instead of the HW3 1021 mux 2 offset. Behavior: read the gateway's current
// raw_mpp value (byte 6 low 5 bits, ×5 = km/h), look up a target km/h via the
// same bucket layout HW3 uses, only write the modified frame back when the
// target is HIGHER than the gateway's current value (never lower). Required
// byte 7 vehicle checksum is recomputed in the handler.
//
// Bucket layout matches HW3 for UX consistency:
//   Custom table (low-speed):  30 / 40 / 50 / 60 / 70 km/h  → user target km/h
//   High-speed table:          80 / 100 / 120 km/h          → user target km/h
//   Cutover at 80 km/h same as HW3.
//
// UI_mppSpeedLimit raw is 5-bit, so max km/h = 31 × 5 = 155 km/h. Values
// above 155 are clamped on write.

#include <cstdint>
#include <algorithm>
#include "can_frame_types.h"
#include "dash_hw3_speed.h"

// Current verified Legacy/HW2.x speed-offset wire path writes UI_userSpeedOffset
// on CAN 760 byte 5 low 6 bits (raw = offset_kph + 30). The field can encode
// raw 0..63, so the positive offset range is 0..33 kph.
inline constexpr uint8_t kLegacySimpleOffsetMaxKph = 33;

inline uint8_t dashClampLegacySimpleOffsetKph(int v)
{
    if (v < 0) v = 0;
    if (v > kLegacySimpleOffsetMaxKph) v = kLegacySimpleOffsetMaxKph;
    return static_cast<uint8_t>(v);
}

inline uint8_t dashComputeLegacySimpleOffsetKph(int fallbackOffsetKph)
{
    uint8_t fl = fusedSpeedLimitRaw;
    if (fl == 0 || fl == 31)
        return dashClampLegacySimpleOffsetKph(fallbackOffsetKph);

    float limitKph = static_cast<float>(fl) * 5.0f;
    float offsetKph = dashComputeOffset(limitKph, 0.05f);
    return dashClampLegacySimpleOffsetKph(static_cast<int>(offsetKph + 0.5f));
}

inline constexpr uint8_t kLegacyMppCustomBucketBaseKph = 30;
inline constexpr uint8_t kLegacyMppCustomBucketStepKph = 10;
inline constexpr uint8_t kLegacyMppCustomTargetCount = 5; // 30/40/50/60/70
inline constexpr uint8_t kLegacyMppHighSpeedBucketBaseKph = 80;
inline constexpr uint8_t kLegacyMppHighSpeedBucketStepKph = 20;
inline constexpr uint8_t kLegacyMppHighSpeedBucketCount = 3; // 80/100/120
inline constexpr uint8_t kLegacyMppCutoverKph = 80;
inline constexpr uint8_t kLegacyMppMaxRaw = 31;          // 5-bit field
inline constexpr uint8_t kLegacyMppMaxKph = 155;         // 31 × 5
inline constexpr uint8_t kLegacyMppCustomTargetMaxByBucket[kLegacyMppCustomTargetCount] = {45, 60, 75, 90, 105};
// Legacy UI_mppSpeedLimit is 5-bit, so the 120 km/h bucket cannot reach the
// HW3 UI cap of 180 km/h. Clamp that bucket to the wire maximum, 155 km/h.
inline constexpr uint8_t kLegacyMppHighSpeedTargetMaxByBucket[kLegacyMppHighSpeedBucketCount] = {120, 150, 155};

// Master toggle plus per-feature toggles (mirrors HW3 layout).
inline bool legacyMppOverride = false;
inline bool legacyMppCustomEnable = false;
inline uint8_t legacyMppCustomTarget[kLegacyMppCustomTargetCount] = {45, 60, 75, 90, 105};
inline bool legacyMppHighSpeedEnable = false;
inline uint8_t legacyMppHighSpeedTarget[kLegacyMppHighSpeedBucketCount] = {90, 110, 130};

// Diagnostic mirrors (read by /status JSON for UI display).
inline uint8_t legacyMppLastRaw = 0;       // last seen raw_mpp from bus
inline uint8_t legacyMppLastSentRaw = 0;   // last raw value we wrote (0 = nothing written)

inline uint8_t dashClampLegacyMppKph(int v)
{
    if (v < 0) v = 0;
    if (v > kLegacyMppMaxKph) v = kLegacyMppMaxKph;
    return static_cast<uint8_t>(v);
}

inline uint8_t dashClampLegacyMppCustomTargetForBucket(uint8_t idx, int v)
{
    if (idx >= kLegacyMppCustomTargetCount)
        return dashClampLegacyMppKph(v);
    if (v < 0) v = 0;
    if (v > kLegacyMppCustomTargetMaxByBucket[idx])
        v = kLegacyMppCustomTargetMaxByBucket[idx];
    return static_cast<uint8_t>(v);
}

inline uint8_t dashClampLegacyMppHighSpeedTargetForBucket(uint8_t idx, int v)
{
    if (idx >= kLegacyMppHighSpeedBucketCount)
        return dashClampLegacyMppKph(v);
    if (v < 0) v = 0;
    if (v > kLegacyMppHighSpeedTargetMaxByBucket[idx])
        v = kLegacyMppHighSpeedTargetMaxByBucket[idx];
    return static_cast<uint8_t>(v);
}

inline bool dashLegacyMppActive()
{
    return legacyMppOverride && (legacyMppCustomEnable || legacyMppHighSpeedEnable);
}

// Returns desired target km/h for a given current raw_mpp km/h, or 0 when no
// override should be applied (no enabled feature covers this bucket).
inline uint16_t dashComputeLegacyMppTargetKph(int currentKph)
{
    if (currentKph < kLegacyMppCustomBucketBaseKph)
        return 0;
    if (currentKph < kLegacyMppCutoverKph)
    {
        if (!legacyMppCustomEnable) return 0;
        uint8_t idx = static_cast<uint8_t>((currentKph - kLegacyMppCustomBucketBaseKph) /
                                            kLegacyMppCustomBucketStepKph);
        if (idx >= kLegacyMppCustomTargetCount) idx = kLegacyMppCustomTargetCount - 1;
        return legacyMppCustomTarget[idx];
    }
    // currentKph >= 80
    if (!legacyMppHighSpeedEnable) return 0;
    uint8_t idx = static_cast<uint8_t>((currentKph - kLegacyMppHighSpeedBucketBaseKph) /
                                        kLegacyMppHighSpeedBucketStepKph);
    if (idx >= kLegacyMppHighSpeedBucketCount) idx = kLegacyMppHighSpeedBucketCount - 1;
    return legacyMppHighSpeedTarget[idx];
}
