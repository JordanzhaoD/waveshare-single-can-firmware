#pragma once

// ──────────────────────────────────────────────────────────────
// dash_bionic_steer.h — Bionic steering torque module
// Replaces fixed 1.80 Nm NagHandler echo with sinusoidal random
// torque that mimics natural human hands-on-wheel behaviour.
//
// Phase 3 of the FSD merge project.
//
// Algorithm: xorshift32 PRNG → random amplitude (30-55), direction
// (±), duration (350-500 ms) → sine-wave perturbation on top of
// the standard echo torque base (0x08B6 ≈ 1.80 Nm).
//
// Safety: perturbation capped at 60 raw units; 3 consecutive
// checksum / frame anomalies → auto-disable bionic, fall back to
// legacy fixed echo.
// ──────────────────────────────────────────────────────────────

#include <cstdint>
#include <cmath>

// ── xorshift32 PRNG ──────────────────────────────────────────

struct DashBionicPRNG
{
    uint32_t s{0xDEADBEEFu};

    void seed(uint32_t v) { s = v ? v : 0xDEADBEEFu; }

    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    /// Uniform integer in [lo, hi]
    uint32_t range(uint32_t lo, uint32_t hi)
    {
        return lo + (next() % (hi - lo + 1));
    }
};

// ── Bionic steering state ────────────────────────────────────

struct DashBionicSteer
{
    // ── tunables ────────────────────────────────────────────
    static constexpr int kPerturbCap{60};    // max |perturbation| (raw)
    static constexpr int kAmplitudeLo{30};   // min random amplitude
    static constexpr int kAmplitudeHi{55};   // max random amplitude
    static constexpr int kDurationLo{350};   // min phase duration (ms)
    static constexpr int kDurationHi{500};   // max phase duration (ms)
    static constexpr int kFramePeriodMs{20}; // ~50 Hz EPAS frame rate
    static constexpr int kMaxConsecutiveFails{3};

    // Standard echo base torque: lower nibble of data[2] | data[3]
    // = 0x08B6 = 2230 raw ≈ 1.80 Nm (signed 12-bit, centre 0x800)
    static constexpr uint16_t kBaseTorqueRaw{0x08B6};

    // ── runtime state ───────────────────────────────────────
    DashBionicPRNG rng;
    float phase{0.0f};
    float phaseStep{0.0f};
    int amplitude{0};
    int direction{1};
    int phaseDurationMs{0};
    int phaseElapsedMs{0};

    int consecutiveFails{0};
    bool disabled{false};

    // ── public API ──────────────────────────────────────────

    /// Seed the PRNG (call once at boot, e.g. with millis()).
    void init(uint32_t seed) { rng.seed(seed); }

    /// True when bionic should NOT be used (fallback to echo).
    bool isDisabled() const { return disabled; }

    /// Reset after user re-enables bionic via Dashboard.
    void reset()
    {
        disabled = false;
        consecutiveFails = 0;
        phaseElapsedMs = phaseDurationMs; // force new phase on next compute
    }

    /// Start a new bionic phase with fresh random parameters.
    void beginPhase()
    {
        amplitude = static_cast<int>(rng.range(kAmplitudeLo, kAmplitudeHi));
        direction = (rng.next() & 1u) ? 1 : -1;
        phaseDurationMs = static_cast<int>(rng.range(kDurationLo, kDurationHi));
        phaseElapsedMs = 0;
        phase = 0.0f;
        // ~2 full sine cycles over the phase duration
        int totalFrames = phaseDurationMs / kFramePeriodMs;
        if (totalFrames < 1)
            totalFrames = 1;
        phaseStep = (2.0f * static_cast<float>(M_PI) * 2.0f) /
                    static_cast<float>(totalFrames);
    }

    /// Compute perturbation for the current frame.
    /// Returns signed perturbation in [-kPerturbCap, +kPerturbCap].
    /// When the current phase is exhausted the caller should call
    /// beginPhase() before the next invocation.
    int computePerturbation()
    {
        if (phaseElapsedMs >= phaseDurationMs)
            return 0;

        float sinVal = sinf(phase);
        int pert = static_cast<int>(amplitude * sinVal) * direction;

        // safety cap
        if (pert > kPerturbCap)
            pert = kPerturbCap;
        if (pert < -kPerturbCap)
            pert = -kPerturbCap;

        phase += phaseStep;
        phaseElapsedMs += kFramePeriodMs;
        return pert;
    }

    /// Apply perturbation to the two torque bytes in the echo frame.
    ///   data2Lo  – lower nibble of data[2] (upper nibble kept by caller)
    ///   data3    – full data[3]
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int perturbation)
    {
        // Decode 12-bit torque: (data2Lo << 8) | data3
        int16_t torque = static_cast<int16_t>(
            (static_cast<uint16_t>(data2Lo) << 8) | data3);
        torque += static_cast<int16_t>(perturbation);

        // Encode back
        data2Lo = static_cast<uint8_t>((torque >> 8) & 0x0F);
        data3 = static_cast<uint8_t>(torque & 0xFF);
    }

    /// Call when a frame checksum / format error is detected.
    void reportFailure()
    {
        consecutiveFails++;
        if (consecutiveFails >= kMaxConsecutiveFails)
            disabled = true;
    }

    /// Call on successful frame injection.
    void reportSuccess() { consecutiveFails = 0; }

    /// True when the current phase is exhausted.
    bool needsNewPhase() const { return phaseElapsedMs >= phaseDurationMs; }
};
