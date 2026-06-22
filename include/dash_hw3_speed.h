#pragma once
// HW3 custom speed-limit boost — config + math helpers + slew limiter.
// Ported from tesla-fsd-controller-main (include/fsd_config.h + include/mod_fsd.h).
//
// Lives in its own header (rather than mcp2515_dashboard.h) so that
// handlers.h can read fusedSpeedLimitRaw and the encode helpers — the include
// order is handlers.h -> ... -> mcp2515_dashboard.h, so dashboard.h is too
// late. Uses C++17 inline globals for cross-TU sharing.
//
// HW3 1021 mux-2 wire format (per tesla project):
//   data[0] bits 6-7 = offset_raw bits 0-1
//   data[1] bits 0-5 = offset_raw bits 2-7
// Two encodings exist for offset_raw → speed boost:
//   KPH5: raw = offsetKph * 5  (legacy fleets)
//   PCT4: raw = pct * 4        (current default)
//
// ── Algorithm versioning ──────────────────────────────────────────────
// USE_NEW_SPEED_ALGO=1 (default): 3-mode + 4-zone segmented lookup from
//   DouyinFSD v3.68. Replaces bucket-based mapping with percentage-based
//   zones + smooth deceleration engine.
// USE_NEW_SPEED_ALGO=0: Original bucket-based mapping (rollback path).
//
// ── NVS / HTTP Parameter Index Mapping ────────────────────────────────
// The custom speed zone percentages use different indexing conventions
// across NVS storage, HTTP API, and C++ arrays. The mapping is:
//
//   Zone (speed range)   C++ array index   NVS key    HTTP param
//   ───────────────────  ────────────────  ─────────  ──────────
//   Zone 0  (≤50 km/h)   customPct[0]      "cp0"      cp1
//   Zone 1  (≤70 km/h)   customPct[1]      "cp1"      cp2
//   Zone 2  (≤100 km/h)  customPct[2]      "cp2"      cp3
//   Zone 3  (>100 km/h)  customPct[3]      "cp3"      cp4
//
//   Rule: NVS key = "cp" + arrayIndex     (0-indexed)
//         HTTP param = "cp" + (arrayIndex + 1)  (1-indexed, user-facing)
//
//   Other speed-related NVS keys:
//     "offsetMode"  → offsetMode       (0=fixed, 1=auto, 2=custom)
//     "manualPct"   → manualOffsetPct  (0-50%, fixed mode)
//     "spd_str"     → dashSpeedStrategy (legacy compat, maps to offsetMode)
//
//   All values validated to 0-50% range via dashClampSpeedCustomPct().

#include <cstdint>
#include <algorithm>
#include "can_frame_types.h"

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h" // millis()
#else
#include <Arduino.h>
#endif
#else
inline uint32_t millis() { return 0; }
#endif

// ─── Algorithm version switch ────────────────────────────────────────────────
#ifndef USE_NEW_SPEED_ALGO
#define USE_NEW_SPEED_ALGO 1 // 1=DouyinFSD v3.68 3-mode algo, 0=old bucket
#endif

// ═══════════════════════════════════════════════════════════════════════════════
//  Shared constants & runtime state (used by both algorithm versions)
// ═══════════════════════════════════════════════════════════════════════════════

inline constexpr uint8_t kHw3SpeedOffsetMaxPct = 50;
inline constexpr uint8_t kHw3WireEncKph5 = 0;
inline constexpr uint8_t kHw3WireEncPct4 = 1;
inline constexpr uint8_t kHw3WireEncDefault = kHw3WireEncPct4;
inline volatile uint8_t hw3WireEncoding = kHw3WireEncDefault;

// ─── Runtime state (live values) ─────────────────────────────────────────────
// Fused/ISA speed limit raw byte from 0x399/921 byte1[4:0] (×5 = kph).
// 0 = SNA, 31 = NONE → no override (stock pass-through).
// volatile: written from CAN task, read by dashComputeHw3OffsetRaw() helpers
// which may be called from web server task for diagnostics.
inline volatile uint8_t fusedSpeedLimitRaw = 0;
// Latest stock offset captured from 1021 mux 0 byte3[1:6] (kph, 0..100).
inline volatile int hw3StockOffsetKph = 0;

// Forward-declare encoding helpers (defined below) for use by both paths.
inline uint8_t dashEncodeHw3OffsetPct4(int pct);
inline uint8_t dashEncodeHw3OffsetKph5(int kph);

// ═══════════════════════════════════════════════════════════════════════════════
#if USE_NEW_SPEED_ALGO
// ═══════════════════════════════════════════════════════════════════════════════
//  NEW: DouyinFSD v3.68 — 3-mode + 4-zone segmented lookup + smooth decel
// ═══════════════════════════════════════════════════════════════════════════════

// ─── New speed system tunables ───────────────────────────────────────────────

// Speed limit below which custom-speed buckets are used (legacy compat).
inline constexpr uint8_t kHw3StockOffsetCutoverKph = 80;

// ─── New runtime state (settings) ────────────────────────────────────────────
// Written from web server task, read from CAN task.
inline volatile uint8_t offsetMode = 1;                  // 0=fixed, 1=auto(default), 2=custom
inline volatile uint8_t manualOffsetPct = 0;             // Fixed mode: 0/10/20/30/40/50%
inline volatile uint8_t customPct[4] = {30, 20, 10, 10}; // 4-zone custom percentages
inline volatile float smoothedOffset = 0.0f;             // Smooth decel tracker (km/h)
inline volatile float actualOffset = 0.0f;               // Current actual offset (km/h)
static constexpr float SMOOTH_RATE = 5.0f;               // Decel smoothing rate km/h/s

// ─── Legacy compatibility shims ──────────────────────────────────────────────
// The web UI and NVS still reference these names. Map them to the new system.
inline volatile bool hw3CustomSpeed = false;     // true when offsetMode==2
inline volatile bool hw3HighSpeedEnable = false; // true when offsetMode!=0
inline volatile bool hw3AutoSpeed = true;        // true when offsetMode==1

// ─── Auto offset: 5-segment lookup table ─────────────────────────────────────
// Matches DouyinFSD v3.68 auto-mode logic exactly.
//
// limit ≤ 40 → target = min(60,  limit × 1.5)  (city/住宅区)
// limit ≤ 60 → target = min(90,  limit × 1.5)  (市区)
// limit ≤ 90 → target = min(117, limit × 1.3)  (快速路)
// limit ≤110 → target = min(132, limit × 1.2)  (高速1)
// limit >110 → target = min(132, limit × 1.1)  (高速2)

inline float dashComputeAutoTarget(float limitKph)
{
    if (limitKph <= 40.0f)
        return std::min(60.0f, limitKph * 1.5f);
    if (limitKph <= 60.0f)
        return std::min(90.0f, limitKph * 1.5f);
    if (limitKph <= 90.0f)
        return std::min(117.0f, limitKph * 1.3f);
    if (limitKph <= 110.0f)
        return std::min(132.0f, limitKph * 1.2f);
    return std::min(132.0f, limitKph * 1.1f);
}

// ─── Custom offset: 4-zone percentage lookup ────────────────────────────────
// Each zone has a user-configurable percentage (0-50%).
//
// limit ≤ 50  → zone 0 (customPct[0])
// limit ≤ 70  → zone 1 (customPct[1])
// limit ≤ 100 → zone 2 (customPct[2])
// limit > 100 → zone 3 (customPct[3])

inline float dashComputeCustomTarget(float limitKph)
{
    uint8_t pct;
    if (limitKph <= 50.0f)
        pct = customPct[0];
    else if (limitKph <= 70.0f)
        pct = customPct[1];
    else if (limitKph <= 100.0f)
        pct = customPct[2];
    else
        pct = customPct[3];
    return limitKph * (1.0f + static_cast<float>(pct) / 100.0f);
}

// ─── Unified offset computation + smooth deceleration engine ─────────────────
// All three modes funnel through here. The smooth decel engine prevents
// sudden speed drops: rising edge passes through instantly, falling edge
// decays at SMOOTH_RATE (5 km/h/s).

inline float dashComputeOffset(float limitKph, float dt)
{
    float target;
    switch (offsetMode)
    {
    case 0: // Fixed percentage
        target = limitKph * (1.0f + static_cast<float>(manualOffsetPct) / 100.0f);
        break;
    case 1: // Auto segmented lookup
        target = dashComputeAutoTarget(limitKph);
        break;
    case 2: // Custom 4-zone
        target = dashComputeCustomTarget(limitKph);
        break;
    default:
        target = limitKph;
        break;
    }
    float rawOffset = target - limitKph;

    // Smooth deceleration engine
    if (rawOffset < smoothedOffset)
    {
        // Falling edge: gradual decay
        smoothedOffset = std::max(rawOffset, smoothedOffset - SMOOTH_RATE * dt);
    }
    else
    {
        // Rising edge: instant follow
        smoothedOffset = rawOffset;
    }
    actualOffset = smoothedOffset;
    return smoothedOffset;
}

// ─── Sync legacy shims when mode changes ─────────────────────────────────────

inline void dashSyncLegacyShims()
{
    hw3AutoSpeed = (offsetMode == 1);
    hw3CustomSpeed = (offsetMode == 2);
    hw3HighSpeedEnable = (offsetMode != 0);
}

// ─── Wire encoding helpers (new path) ────────────────────────────────────────

inline uint8_t dashEncodeHw3OffsetFromPct(int pct, uint8_t flKph)
{
    if (pct <= 0 || flKph == 0)
        return 0;
    if (hw3WireEncoding == kHw3WireEncPct4)
    {
        return dashEncodeHw3OffsetPct4(pct);
    }
    int offsetKph = (static_cast<int>(flKph) * pct + 50) / 100;
    return dashEncodeHw3OffsetKph5(offsetKph);
}

// ─── Main offset computation (new path) ──────────────────────────────────────
// Replaces the old bucket-based dashComputeHw3OffsetRaw().
// Uses dashComputeOffset() with a 50ms dt assumption for the main loop.

inline uint8_t dashComputeHw3OffsetRaw(int stockOffsetRaw)
{
    uint8_t fl = fusedSpeedLimitRaw;
    if (fl == 0 || fl == 31)
    { // SNA / NONE: pass through current mux-2 raw and clear telemetry.
        actualOffset = 0.0f;
        smoothedOffset = 0.0f;
        return static_cast<uint8_t>(std::max(std::min(stockOffsetRaw, 255), 0));
    }
    float flKph = static_cast<float>(fl) * 5.0f;

    // Compute offset with 50ms timestep (main loop ~20Hz)
    float offsetKph = dashComputeOffset(flKph, 0.05f);

    if (offsetKph <= 0.0f)
        return 0;

    int pct = static_cast<int>((offsetKph / flKph) * 100.0f + 0.5f);
    if (pct > kHw3SpeedOffsetMaxPct)
        pct = kHw3SpeedOffsetMaxPct;
    return dashEncodeHw3OffsetFromPct(pct, static_cast<uint8_t>(flKph));
}

// ─── Compatibility: hw3 active check ─────────────────────────────────────────

inline bool dashHw3CustomSpeedActive()
{
    return offsetMode != 0; // Any non-fixed mode is "active"
}

// ─── Legacy API compatibility shims ──────────────────────────────────────────
// These provide the old constants/variables/functions that mcp2515_dashboard.h
// still references. They map to the new 3-mode+4-zone system.
// Will be removed once dashboard.h is fully migrated (Task 2).

// Old constants (dashboard.h loops over these for NVS/JSON)
inline constexpr uint8_t kHw3CustomTargetCount = 5;
inline constexpr uint8_t kHw3HighSpeedBucketCount = 3;
inline constexpr uint8_t kHw3CustomBucketBaseKph = 30;
inline constexpr uint8_t kHw3CustomBucketStepKph = 10;
inline constexpr uint8_t kHw3HighSpeedBucketBaseKph = 80;
inline constexpr uint8_t kHw3HighSpeedBucketStepKph = 20;
inline constexpr uint8_t kHw3CustomTargetMaxKph = 160;
inline constexpr uint8_t kHw3HighSpeedTargetMaxKph = 200;
inline constexpr uint8_t kHw3CustomTargetMaxByBucket[kHw3CustomTargetCount] = {45, 60, 75, 90, 105};
inline constexpr uint8_t kHw3HighSpeedTargetMaxByBucket[kHw3HighSpeedBucketCount] = {120, 150, 180};
inline constexpr uint8_t kHw3AutoTargetBelow60Kph = 64;
inline constexpr uint8_t kHw3AutoTargetAt60Kph = 100;
inline constexpr uint8_t kHw3AutoTargetForVisible80Kph = 85;

// Old-style bucket arrays — kept for NVS migration and JSON output.
// In the new system these are derived from customPct[4] + auto lookup,
// but we maintain separate storage so NVS round-trips work during migration.
inline volatile uint8_t hw3CustomTarget[kHw3CustomTargetCount] = {45, 60, 75, 90, 105};
inline volatile uint8_t hw3HighSpeedTarget[kHw3HighSpeedBucketCount] = {90, 110, 130};

// High-speed bucket verified (5-bucket) — kept for NVS compat
inline constexpr uint8_t kHw3HighSpeedBucketBaseKph_verified = 80;
inline constexpr uint8_t kHw3HighSpeedBucketStepKph_verified = 10;
inline constexpr uint8_t kHw3HighSpeedBucketCount_verified = 5;
inline volatile uint8_t hw3HighSpeedTargetPct[kHw3HighSpeedBucketCount_verified] = {25, 25, 25, 25, 25};

// Old clamp helpers (dashboard.h uses these for NVS and POST validation)
inline uint8_t dashClampHw3CustomTargetKph(int v)
{
    if (v < 0)
        v = 0;
    if (v > (int)kHw3CustomTargetMaxKph)
        v = (int)kHw3CustomTargetMaxKph;
    return static_cast<uint8_t>(v);
}
inline uint8_t dashClampHw3HighSpeedTargetKph(int v)
{
    if (v < 0)
        v = 0;
    if (v > (int)kHw3HighSpeedTargetMaxKph)
        v = (int)kHw3HighSpeedTargetMaxKph;
    return static_cast<uint8_t>(v);
}
inline uint8_t dashClampHw3CustomTargetForBucket(uint8_t idx, int v)
{
    uint8_t maxKph = idx < kHw3CustomTargetCount ? kHw3CustomTargetMaxByBucket[idx] : kHw3CustomTargetMaxKph;
    if (v < 0)
        v = 0;
    if (v > maxKph)
        v = maxKph;
    return static_cast<uint8_t>(v);
}
inline uint8_t dashClampHw3HighSpeedTargetForBucket(uint8_t idx, int v)
{
    uint8_t maxKph = idx < kHw3HighSpeedBucketCount ? kHw3HighSpeedTargetMaxByBucket[idx] : kHw3HighSpeedTargetMaxKph;
    if (v < 0)
        v = 0;
    if (v > maxKph)
        v = maxKph;
    return static_cast<uint8_t>(v);
}

// Old auto/compute helpers (dashboard.h calls these for status JSON)
inline uint8_t dashComputeHw3AutoTargetKph(uint8_t fusedLimitKph)
{
    return static_cast<uint8_t>(dashComputeAutoTarget(static_cast<float>(fusedLimitKph)));
}
inline uint16_t dashComputeHw3CustomTargetKph(uint8_t flKph)
{
    if (flKph < kHw3CustomBucketBaseKph)
        return 0;
    if (flKph >= kHw3StockOffsetCutoverKph)
        return 0;
    return static_cast<uint16_t>(dashComputeCustomTarget(static_cast<float>(flKph)));
}

// Old encode helper (used by some legacy code paths)
inline uint8_t dashEncodeHw3Offset(int offsetKph, uint8_t flKph)
{
    if (hw3WireEncoding == kHw3WireEncPct4)
    {
        if (flKph == 0)
            return 0;
        int pct = (offsetKph * 100 + flKph / 2) / flKph;
        return dashEncodeHw3OffsetPct4(pct);
    }
    return dashEncodeHw3OffsetKph5(offsetKph);
}

// ═══════════════════════════════════════════════════════════════════════════════
#else // !USE_NEW_SPEED_ALGO — Original bucket-based algorithm
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Tunables ────────────────────────────────────────────────────────────────
inline constexpr uint8_t kHw3CustomBucketBaseKph = 30;
inline constexpr uint8_t kHw3CustomBucketStepKph = 10;
inline constexpr uint8_t kHw3CustomTargetCount = 5; // 30/40/50/60/70
inline constexpr uint8_t kHw3StockOffsetCutoverKph = 80;
inline constexpr uint8_t kHw3HighSpeedBucketBaseKph = 80;
inline constexpr uint8_t kHw3HighSpeedBucketStepKph = 20;
inline constexpr uint8_t kHw3HighSpeedBucketCount = 3; // 80/100/120
inline constexpr uint8_t kHw3CustomTargetMaxKph = 160;
inline constexpr uint8_t kHw3HighSpeedTargetMaxKph = 200;
inline constexpr uint8_t kHw3CustomTargetMaxByBucket[kHw3CustomTargetCount] = {45, 60, 75, 90, 105};
inline constexpr uint8_t kHw3HighSpeedTargetMaxByBucket[kHw3HighSpeedBucketCount] = {120, 150, 180};

// ─── Runtime state (settings) ────────────────────────────────────────────────
inline volatile bool hw3CustomSpeed = false;
inline volatile uint8_t hw3CustomTarget[kHw3CustomTargetCount] = {45, 60, 75, 90, 105};
inline volatile bool hw3HighSpeedEnable = false;
inline volatile uint8_t hw3HighSpeedTarget[kHw3HighSpeedBucketCount] = {90, 110, 130};

// --- HW3 auto speed targeting (from tesla-fsd-controller fsd_config.h) ---
inline constexpr uint8_t kHw3AutoTargetBelow60Kph = 64;
inline constexpr uint8_t kHw3AutoTargetAt60Kph = 100;
inline constexpr uint8_t kHw3AutoTargetForVisible80Kph = 85;

inline volatile bool hw3AutoSpeed = true;

// Dashboard API compatibility state for USE_NEW_SPEED_ALGO=0 rollback builds.
// These names are read/written by mcp2515_dashboard.h even when the old bucket
// algorithm is selected. The old compute path below remains bucket-based.
inline volatile uint8_t offsetMode = 1;                  // 0=fixed, 1=auto(default), 2=custom
inline volatile uint8_t manualOffsetPct = 0;             // accepted for API/NVS compatibility
inline volatile uint8_t customPct[4] = {30, 20, 10, 10}; // accepted for API/NVS compatibility
inline volatile float actualOffset = 0.0f;               // old path does not compute this metric

inline uint8_t dashComputeHw3AutoTargetKph(uint8_t fusedLimitKph)
{
    if (fusedLimitKph == 60)
        return kHw3AutoTargetAt60Kph;
    if (fusedLimitKph < kHw3AutoTargetBelow60Kph)
        return kHw3AutoTargetBelow60Kph;
    if (fusedLimitKph < kHw3StockOffsetCutoverKph)
        return kHw3AutoTargetForVisible80Kph;
    return fusedLimitKph;
}

// High-speed bucket configuration (tesla-fsd-controller: 5 buckets at 10kph step)
inline constexpr uint8_t kHw3HighSpeedBucketBaseKph_verified = 80;
inline constexpr uint8_t kHw3HighSpeedBucketStepKph_verified = 10;
inline constexpr uint8_t kHw3HighSpeedBucketCount_verified = 5;

inline volatile uint8_t hw3HighSpeedTargetPct[kHw3HighSpeedBucketCount_verified] = {25, 25, 25, 25, 25};

// Offset from pct for high-speed mode
inline uint8_t dashEncodeHw3OffsetFromPct(int pct, uint8_t flKph)
{
    if (pct <= 0 || flKph == 0)
        return 0;
    if (hw3WireEncoding == kHw3WireEncPct4)
    {
        return dashEncodeHw3OffsetPct4(pct);
    }
    int offsetKph = (static_cast<int>(flKph) * pct + 50) / 100;
    return dashEncodeHw3OffsetKph5(offsetKph);
}

// ─── Math helpers (old path) ─────────────────────────────────────────────────

inline uint8_t dashClampHw3HighSpeedTargetKph(int v)
{
    if (v < 0)
        v = 0;
    if (v > kHw3HighSpeedTargetMaxKph)
        v = kHw3HighSpeedTargetMaxKph;
    return static_cast<uint8_t>(v);
}

inline uint8_t dashClampHw3CustomTargetKph(int v)
{
    if (v < 0)
        v = 0;
    if (v > kHw3CustomTargetMaxKph)
        v = kHw3CustomTargetMaxKph;
    return static_cast<uint8_t>(v);
}

inline uint8_t dashClampHw3CustomTargetForBucket(uint8_t idx, int v)
{
    uint8_t maxKph = idx < kHw3CustomTargetCount ? kHw3CustomTargetMaxByBucket[idx] : kHw3CustomTargetMaxKph;
    if (v < 0)
        v = 0;
    if (v > maxKph)
        v = maxKph;
    return static_cast<uint8_t>(v);
}

inline uint8_t dashClampHw3HighSpeedTargetForBucket(uint8_t idx, int v)
{
    uint8_t maxKph = idx < kHw3HighSpeedBucketCount ? kHw3HighSpeedTargetMaxByBucket[idx] : kHw3HighSpeedTargetMaxKph;
    if (v < 0)
        v = 0;
    if (v > maxKph)
        v = maxKph;
    return static_cast<uint8_t>(v);
}

// "Custom" target — looks up hw3CustomTarget[idx] for the 30/40/50/60/70 kph
// bucket containing flKph. Returns 0 outside the supported range.
inline uint16_t dashComputeHw3CustomTargetKph(uint8_t flKph)
{
    if (flKph < kHw3CustomBucketBaseKph)
        return 0;
    if (flKph >= kHw3StockOffsetCutoverKph)
        return 0;
    uint8_t idx = static_cast<uint8_t>((flKph - kHw3CustomBucketBaseKph) /
                                       kHw3CustomBucketStepKph);
    if (idx >= kHw3CustomTargetCount)
        idx = kHw3CustomTargetCount - 1;
    return hw3CustomTarget[idx];
}

// ─── Main offset computation (old path) ──────────────────────────────────────

inline uint8_t dashComputeHw3OffsetRaw(int stockOffsetRaw)
{
    uint8_t fl = fusedSpeedLimitRaw;
    if (fl == 0 || fl == 31)
    { // SNA / NONE: pass through current mux-2 raw and clear telemetry.
        actualOffset = 0.0f;
        return static_cast<uint8_t>(std::max(std::min(stockOffsetRaw, 255), 0));
    }
    uint16_t flKph = static_cast<uint16_t>(fl) * 5;

    int desiredOffsetKph = stockOffsetRaw;

    if (flKph < kHw3StockOffsetCutoverKph && hw3CustomSpeed)
    {
        uint16_t target = dashComputeHw3CustomTargetKph(static_cast<uint8_t>(flKph));
        if (target > flKph)
            desiredOffsetKph = static_cast<int>(target - flKph);
    }
    else if (flKph >= kHw3StockOffsetCutoverKph && hw3HighSpeedEnable)
    {
        uint8_t idx = static_cast<uint8_t>((flKph - kHw3HighSpeedBucketBaseKph) /
                                           kHw3HighSpeedBucketStepKph);
        if (idx >= kHw3HighSpeedBucketCount)
            idx = kHw3HighSpeedBucketCount - 1;
        uint8_t targetKph = hw3HighSpeedTarget[idx];
        desiredOffsetKph = targetKph > flKph ? static_cast<int>(targetKph - flKph) : 0;
    }
    return dashEncodeHw3Offset(desiredOffsetKph, static_cast<uint8_t>(flKph));
}

inline bool dashHw3CustomSpeedActive()
{
    return hw3CustomSpeed || hw3HighSpeedEnable;
}

inline uint8_t dashEncodeHw3Offset(int offsetKph, uint8_t flKph)
{
    if (hw3WireEncoding == kHw3WireEncPct4)
    {
        if (flKph == 0)
            return 0;
        int pct = (offsetKph * 100 + flKph / 2) / flKph;
        return dashEncodeHw3OffsetPct4(pct);
    }
    return dashEncodeHw3OffsetKph5(offsetKph);
}

// ─── Compatibility helpers for new dashboard API used by dashboard.h ─────────
inline float dashComputeOffset(float, float) { return 0.0f; }
inline void dashSyncLegacyShims()
{
    if (offsetMode > 2)
        offsetMode = 1;
    hw3AutoSpeed = (offsetMode == 1);
    hw3CustomSpeed = (offsetMode == 2);
    hw3HighSpeedEnable = (offsetMode != 0);
}

#endif // USE_NEW_SPEED_ALGO

// ═══════════════════════════════════════════════════════════════════════════════
//  Shared: Wire encoding primitives (always available)
// ═══════════════════════════════════════════════════════════════════════════════

inline uint8_t dashEncodeHw3OffsetPct4(int pct)
{
    if (pct < 0)
        pct = 0;
    if (pct > kHw3SpeedOffsetMaxPct)
        pct = kHw3SpeedOffsetMaxPct;
    return static_cast<uint8_t>(pct * 4);
}

inline uint8_t dashEncodeHw3OffsetKph5(int kph)
{
    if (kph < 0)
        kph = 0;
    if (kph > 40)
        kph = 40;
    return static_cast<uint8_t>(kph * 5);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Shared: 1021 mux-2 wire codec
// ═══════════════════════════════════════════════════════════════════════════════

inline bool dashReadHw3OffsetRawShared(const CanFrame &frame, uint8_t &raw)
{
    if (frame.id != 1021 || frame.dlc < 2)
        return false;
    raw = static_cast<uint8_t>(((frame.data[1] & 0x3F) << 2) | ((frame.data[0] >> 6) & 0x03));
    return true;
}

inline void dashWriteHw3OffsetRawShared(CanFrame &frame, uint8_t raw)
{
    frame.data[0] = static_cast<uint8_t>((frame.data[0] & ~0xC0) | ((raw & 0x03) << 6));
    frame.data[1] = static_cast<uint8_t>((frame.data[1] & ~0x3F) | (raw >> 2));
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Shared: Slew limiter (rate now fixed at 5 km/h/s matching smooth engine)
// ═══════════════════════════════════════════════════════════════════════════════
// Damps drops in the wire offset to avoid sudden braking when the AP-fused
// limit suddenly drops (e.g. transitioning into a school zone). Only limits
// downward motion; rising edge passes through immediately. Kept here (rather
// than in mcp2515_dashboard.h) so HW3Handler::handleMessage can call it
// directly without taking a dependency on dashboard.h.

inline constexpr uint8_t kHw3SlewRateMin = 1;
inline constexpr uint8_t kHw3SlewRateMax = 25;
inline constexpr uint8_t kHw3SlewRateDefault = 25;

// volatile: hw3OffsetSlew and hw3SlewRate are written from web server task,
// the rest are CAN-task-only but marked volatile for diagnostic reads.
inline volatile bool hw3OffsetSlew = false;
inline volatile uint8_t hw3SlewRate = kHw3SlewRateDefault;
inline volatile uint8_t hw3OffsetTargetRaw = 0;
inline volatile uint8_t hw3OffsetLastRaw = 0;
inline volatile uint32_t hw3OffsetLastSentMs = 0;
inline volatile uint32_t hw3OffsetSlewCount = 0;

inline uint8_t dashClampHw3SlewRate(int rate)
{
    if (rate < kHw3SlewRateMin)
        return kHw3SlewRateMin;
    if (rate > kHw3SlewRateMax)
        return kHw3SlewRateMax;
    return static_cast<uint8_t>(rate);
}

inline uint8_t dashLoadHw3SlewRate(uint8_t rate)
{
    if (rate < kHw3SlewRateMin || rate > kHw3SlewRateMax)
        return kHw3SlewRateDefault;
    return rate;
}

// Reads the current raw offset out of `modified`, records it as the active
// target, and (when hw3OffsetSlew is enabled) clamps any decrease to
// kHw3SlewRate*4 raw-units/sec. Returns true if the value was modified.
inline bool dashApplyHw3OffsetSlew(CanFrame &modified, const CanFrame & /*original*/)
{
    uint8_t activeRaw = 0;
    if (!dashReadHw3OffsetRawShared(modified, activeRaw))
        return false;
    // Mux-2 only: 1021 in this codebase uses readMuxID() = data[0] & 0x07.
    // Note we read this BEFORE writing back the offset, since the offset
    // bits live in data[0] high nibble which doesn't overlap the mux id.
    if ((modified.data[0] & 0x07) != 2)
        return false;

    hw3OffsetTargetRaw = activeRaw;
    uint8_t shapedRaw = activeRaw;
    uint32_t now = millis();

    if (hw3OffsetSlew)
    {
        uint8_t last = hw3OffsetLastRaw;
        if (activeRaw < last && hw3OffsetLastSentMs != 0)
        {
            uint32_t rateRawPerSec = static_cast<uint32_t>(dashLoadHw3SlewRate(hw3SlewRate)) * 4;
            uint32_t dt = now - hw3OffsetLastSentMs;
            uint32_t maxDrop = (rateRawPerSec * dt + 500) / 1000;
            uint8_t floorRaw = last > maxDrop ? static_cast<uint8_t>(last - maxDrop) : 0;
            if (activeRaw < floorRaw)
            {
                shapedRaw = floorRaw;
                hw3OffsetSlewCount = hw3OffsetSlewCount + 1;
            }
        }
    }

    hw3OffsetLastRaw = shapedRaw;
    hw3OffsetLastSentMs = now;
    if (shapedRaw == activeRaw)
        return false;

    dashWriteHw3OffsetRawShared(modified, shapedRaw);
    return true;
}
