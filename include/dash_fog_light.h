#pragma once

// ──────────────────────────────────────────────────────────────
// dash_fog_light.h — Rear fog light stunt controller
// Sends 0x273 CAN frames via CAN-B (MCP2515) to control the rear
// fog light for strobe / F1 pilot / continuous patterns.
//
// Phase 4 of the FSD merge project.
//
// Modes:
//   0 = OFF
//   1 = Strobe  — data[2] toggles ON/OFF at configurable rate
//   2 = F1 Pilot — 3-flash burst (135 ms each) + 1500 ms pause
//   3 = Continuous — data[2] held ON (keep-alive every 500ms)
//
// Safety: only activates in Drive gear (gearRaw == 4).
// Auto-stops on: gear change or explicit stop().
// ──────────────────────────────────────────────────────────────

#include <cstdint>
#include "can_frame_types.h"

struct DashFogLight
{
    // ── mode constants ──────────────────────────────────────
    static constexpr int kModeOff{0};
    static constexpr int kModeStrobe{1};
    static constexpr int kModeF1Pilot{2};
    static constexpr int kModeContinuous{3};

    // ── timing ──────────────────────────────────────────────
    static constexpr int kF1FlashDurationMs{135};
    static constexpr int kF1FlashCount{3};
    static constexpr int kF1PauseMs{1500};
    static constexpr int kKeepAliveMs{500};

    // ── runtime state ───────────────────────────────────────
    int mode{kModeOff};
    int strobeCount{0};       // total flashes requested (0 = infinite)
    int strobeTogglesDone{0}; // completed ON+OFF toggle pairs
    int frequency{1};         // 0=slow(100ms), 1=medium(50ms), 2=fast(30ms)
    bool strobeOn{false};
    int tickAccumMs{0};

    // F1 state machine
    int f1FlashIndex{0}; // current flash in burst (0..kF1FlashCount-1)
    int f1PhaseMs{0};    // ms elapsed in current flash phase
    bool f1InPause{false};

    // ── public API ──────────────────────────────────────────

    int toggleIntervalMs() const
    {
        switch (frequency)
        {
        case 0:
            return 100;
        case 2:
            return 30;
        default:
            return 50;
        }
    }

    void startStrobe(int count = 0, int freq = 1)
    {
        mode = kModeStrobe;
        strobeCount = count;
        strobeTogglesDone = 0;
        frequency = freq;
        strobeOn = false;
        tickAccumMs = 0;
    }

    void startF1Pilot()
    {
        mode = kModeF1Pilot;
        f1FlashIndex = 0;
        f1PhaseMs = 0;
        f1InPause = false;
        tickAccumMs = 0;
    }

    void startContinuous()
    {
        mode = kModeContinuous;
        tickAccumMs = 0;
    }

    void stop()
    {
        mode = kModeOff;
        strobeOn = false;
        f1InPause = false;
    }

    bool isActive() const { return mode != kModeOff; }

    /// Tick the state machine.  Returns true + fills outData when
    /// a CAN frame should be sent.  gearRaw from DI_systemStatus
    /// (4 = Drive); any other value auto-stops.
    bool tick(int elapsedMs, uint8_t gearRaw, uint8_t outData[8])
    {
        if (gearRaw != 4)
        {
            if (isActive())
                stop();
            return false;
        }
        if (mode == kModeOff)
            return false;

        tickAccumMs += elapsedMs;

        switch (mode)
        {
        case kModeStrobe:
            return tickStrobe(outData);
        case kModeF1Pilot:
            return tickF1(outData);
        case kModeContinuous:
            return tickContinuous(outData);
        default:
            return false;
        }
    }

    /// Build a 0x273 CAN frame.
    static void buildFrame(uint8_t data[8], bool fogOn)
    {
        data[0] = FOG_BASE_0;
        data[1] = FOG_BASE_1;
        data[2] = fogOn ? FOG_BASE_2_ON : FOG_BASE_2_OFF;
        data[3] = FOG_BASE_3;
        data[4] = FOG_BASE_4;
        data[5] = FOG_BASE_5;
        data[6] = FOG_BASE_6;
        // Checksum: sum(CAN ID bytes) + sum(data[0..6])
        uint16_t sum = 0x73u + 0x02u; // 0x273: low=0x73, high=0x02
        for (int i = 0; i < 7; ++i)
            sum += data[i];
        data[7] = static_cast<uint8_t>(sum & 0xFF);
    }

private:
    bool tickStrobe(uint8_t outData[8])
    {
        if (tickAccumMs < toggleIntervalMs())
            return false;
        tickAccumMs -= toggleIntervalMs();

        strobeOn = !strobeOn;
        if (!strobeOn)
            strobeTogglesDone++;

        // Count limit (0 = infinite)
        if (strobeCount > 0 && strobeTogglesDone >= strobeCount)
        {
            stop();
            buildFrame(outData, false);
            return true;
        }

        buildFrame(outData, strobeOn);
        return true;
    }

    bool tickF1(uint8_t outData[8])
    {
        if (f1InPause)
        {
            if (tickAccumMs < kF1PauseMs)
                return false;
            tickAccumMs -= kF1PauseMs;
            f1InPause = false;
            f1FlashIndex = 0;
            f1PhaseMs = 0;
            return false;
        }

        if (tickAccumMs < kF1FlashDurationMs / 2)
            return false;

        // Each flash: ON for half duration, OFF for half duration
        int halfMs = kF1FlashDurationMs / 2;
        bool fogOn = (f1PhaseMs < halfMs);
        f1PhaseMs += halfMs;
        tickAccumMs -= halfMs;

        // Transition to OFF at midpoint → advance flash index
        if (!fogOn)
        {
            f1FlashIndex++;
            if (f1FlashIndex >= kF1FlashCount)
            {
                f1InPause = true;
                tickAccumMs = 0;
            }
        }

        buildFrame(outData, fogOn);
        return true;
    }

    bool tickContinuous(uint8_t outData[8])
    {
        if (tickAccumMs < kKeepAliveMs)
            return false;
        tickAccumMs -= kKeepAliveMs;
        buildFrame(outData, true);
        return true;
    }
};
