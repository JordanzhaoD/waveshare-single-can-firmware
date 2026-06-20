#pragma once

// ──────────────────────────────────────────────────────────────
// dash_wheel_dnd.h — Wheel DND (Do-Not-Disturb) elimination
// Sends 0x3C2 four-step sequences via CAN-B (MCP2515) to suppress
// volume and speed-control scroll-wheel notifications.
//
// Phase 3 of the FSD merge project.
//
// Two independent sub-features:
//   1. Volume DND  – data[2] sequence: 01→00→3F→00 (each step 50 ms)
//   2. Speed  DND  – data[3] sequence: 01→00→3F→00 (each step 50 ms)
//
// Both share the same 0x3C2 frame; they are OR-combined when both
// are active. Counter increments each frame; Tesla checksum is
// recalculated.
//
// Gated by: dashDefenseEnabled && (dashDndVolume || dashDndSpeed)
// ──────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>

#include "dash_tx_evidence.h"

struct DashWheelDndDiag
{
    bool native3c2Seen = false;
    uint32_t native3c2Ms = 0;
    const char *baseFrameMode = "waiting_native";
    const char *sequenceState = "idle";
    uint8_t sequenceStep = 0;
    DashTxEvidence tx{};
};

struct DashWheelDND
{
    // ── sequence constants ───────────────────────────────────
    static constexpr uint8_t kSteps[]{0x01, 0x00, 0x3F, 0x00};
    static constexpr int kStepCount{4};
    static constexpr int kStepIntervalMs{50};    // delay between steps
    static constexpr uint32_t kCanId{0x3C2};     // wheel controls

    // ── runtime state ────────────────────────────────────────
    int volumeStep{0};
    int speedStep{0};
    bool volumeActive{false};
    bool speedActive{false};
    int lastStepMs{0};  // timestamp of last step (millis)
    uint8_t counter{0}; // rolling counter for 0x3C2 frames
    uint8_t nativeData[8] = {};
    uint32_t nativeMs = 0;
    bool nativeSeen = false;
    bool syntheticFallback = false;
    DashWheelDndDiag diag{};

    // ── public API ──────────────────────────────────────────

    /// Start the volume DND sequence.
    void startVolume()
    {
        volumeActive = true;
        volumeStep = 0;
    }

    /// Start the speed DND sequence.
    void startSpeed()
    {
        speedActive = true;
        speedStep = 0;
    }

    void recordNative3c2(const uint8_t data[8], uint32_t nowMs)
    {
        memcpy(nativeData, data, 8);
        nativeMs = nowMs;
        nativeSeen = true;
        diag.native3c2Seen = true;
        diag.native3c2Ms = nowMs;
    }

    bool native3c2Fresh(uint32_t nowMs) const
    {
        return nativeSeen && (nowMs - nativeMs) <= 500;
    }

    /// Tick the state machine.  Call from the main loop (or a
    /// FreeRTOS task) with the current millis() value.
    /// Returns true when a CAN frame should be sent this tick;
    /// the frame bytes are written to `outData[8]`.
    bool tick(int nowMs, uint8_t outData[8])
    {
        if (!volumeActive && !speedActive)
            return false;

        if (nowMs - lastStepMs < kStepIntervalMs)
            return false;

        lastStepMs = nowMs;

        bool useNative = native3c2Fresh((uint32_t)nowMs);
        if (!useNative && !syntheticFallback)
        {
            diag.baseFrameMode = "waiting_native";
            diag.sequenceState = "waiting_native";
            return false;
        }

        if (useNative)
        {
            memcpy(outData, nativeData, 8);
            diag.baseFrameMode = "native";
        }
        else
        {
            memset(outData, 0, 8);
            diag.baseFrameMode = "synthetic_fallback";
        }
        diag.sequenceState = "running";

        // Volume step
        if (volumeActive)
        {
            outData[2] = kSteps[volumeStep];
            volumeStep++;
            if (volumeStep >= kStepCount)
            {
                volumeActive = false;
                volumeStep = 0;
            }
        }

        // Speed step
        if (speedActive)
        {
            outData[3] = kSteps[speedStep];
            speedStep++;
            if (speedStep >= kStepCount)
            {
                speedActive = false;
                speedStep = 0;
            }
        }

        // Advance counter
        counter = static_cast<uint8_t>((counter + 1) & 0x0F);
        outData[0] = counter;

        // Tesla checksum (sum of ID bytes + data[0..6])
        // For 0x3C2: low byte = 0xC2, high byte = 0x03
        uint16_t sum = 0xC2u + 0x03u;
        for (int i = 0; i < 7; ++i)
            sum += outData[i];
        outData[7] = static_cast<uint8_t>(sum & 0xFF);

        return true;
    }

    /// True when either sequence is still running.
    bool isRunning() const { return volumeActive || speedActive; }

    /// Reset all state.
    void reset()
    {
        volumeActive = false;
        speedActive = false;
        volumeStep = 0;
        speedStep = 0;
        counter = 0;
    }
};
