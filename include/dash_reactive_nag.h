#pragma once

// dash_reactive_nag.h — Reactive NAG-suppression torque burst
// Port of public-source web_dnd_steer, Legacy-adapted.
//
// Mechanism: detect NAG via 0x399 byte5 bits[5:2] >= 3, then inject a bounded
// half-sine torque burst on 0x370 data[2:3] (Legacy torque field). 2-3 strokes,
// 3 bursts then 3 s cooldown.
//
// Pure logic: all time passed explicitly as nowMs (testable; native
// dashDiagNowMs() is a per-call counter, not real ms).

#include <cstdint>
#include <cmath>

struct DashReactivePRNG
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
    uint32_t range(uint32_t lo, uint32_t hi) { return (hi < lo) ? lo : lo + (next() % (hi - lo + 1)); }
};

// POD snapshot of reactive-NAG runtime state for /status + serial diag.
// No String — keeps it native-compilable. `enabled` is filled by the handler
// (the engine does not know about the bionicSteering toggle).
struct DashReactiveDiag
{
    bool enabled{false};
    bool nagActive{false};
    bool injecting{false};
    int burstsThisCycle{0};
    int lastAmplitude{0};
    int lastHandsOnState{0};
    unsigned long cooldownRemainMs{0};
    // Instrumentation counters (gate-evaluation diagnostics). Let the device
    // self-diagnose WHY a burst did/didn't fire across a drive.
    uint32_t nagSamples{0};      // onNagSample calls with hos>=threshold
    uint32_t burstsStarted{0};   // bursts actually started
    uint32_t blockedCooldown{0}; // blocked: nowMs < cooldownUntilMs
    uint32_t blockedGap{0};      // blocked: within kBurstGapMs of last burst
    uint32_t echoSent{0};        // 0x370 reactive echoes actually transmitted
};

struct DashReactiveNagBurst
{
    // tunables (public-source-faithful + Legacy cap)
    static constexpr int kHumanWeight{8};
    static constexpr int kNagThreshold{3};
    static constexpr int kAmpLight{60};
    static constexpr int kAmpLightJitter{15};
    static constexpr int kAmpHeavy{80};
    static constexpr int kAmpHeavyJitter{15};
    static constexpr int kAmplitudeCap{95};
    static constexpr int kStrokesMin{2};
    static constexpr int kStrokesMax{3};
    static constexpr int kStrokeDurLo{300};
    static constexpr int kStrokeDurHi{400};
    static constexpr int kMaxBursts{3};
    static constexpr unsigned long kBurstGapMs{1500};
    static constexpr unsigned long kCooldownMs{3000};

    // runtime state
    DashReactivePRNG rng;
    bool injecting{false};
    unsigned long waveStartMs{0};
    int strokeCount{0};
    int totalStrokes{2};
    int direction{1};
    int amplitude{80};
    int strokeDurMs{350};
    int burstsThisCycle_{0};
    unsigned long lastBurstMs{0};
    unsigned long cooldownUntilMs{0};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};
    int lastAmplitude_{0};
    // instrumentation counters
    uint32_t nagSamples_{0};
    uint32_t burstsStarted_{0};
    uint32_t blockedCooldown_{0};
    uint32_t blockedGap_{0};
    uint32_t echoSent_{0};

    void init(uint32_t seed) { rng.seed(seed); }
    void reset()
    {
        injecting = false;
        strokeCount = 0;
        burstsThisCycle_ = 0;
        lastBurstMs = 0;
        cooldownUntilMs = 0;
        lastHandsOnState = 0;
        nagActive_ = false;
        nagSamples_ = 0;
        burstsStarted_ = 0;
        blockedCooldown_ = 0;
        blockedGap_ = 0;
        echoSent_ = 0;
    }

    void notifyEchoSent() { echoSent_++; }

    // Instrumentation-only: zero / load the persistent counters (NVS round-trip).
    // Separate from reset() so toggling the feature does not wipe a measurement.
    void resetCounters()
    {
        nagSamples_ = 0;
        burstsStarted_ = 0;
        blockedCooldown_ = 0;
        blockedGap_ = 0;
        echoSent_ = 0;
    }
    void setCounters(uint32_t ns, uint32_t bs, uint32_t bc, uint32_t bg, uint32_t es)
    {
        nagSamples_ = ns;
        burstsStarted_ = bs;
        blockedCooldown_ = bc;
        blockedGap_ = bg;
        echoSent_ = es;
    }

    bool isNagActive() const { return nagActive_; }
    bool shouldInject(unsigned long /*nowMs*/) const { return injecting; }
    int burstsThisCycle() const { return burstsThisCycle_; }
    int lastAmplitude() const { return lastAmplitude_; }
    int amplitudeCap() const { return kAmplitudeCap; }
    unsigned long cooldownRemainingMs(unsigned long nowMs) const
    {
        return (nowMs < cooldownUntilMs) ? (cooldownUntilMs - nowMs) : 0UL;
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        return {false, nagActive_, injecting, burstsThisCycle_,
                lastAmplitude_, (int)lastHandsOnState, cooldownRemainingMs(nowMs),
                nagSamples_, burstsStarted_, blockedCooldown_, blockedGap_, echoSent_};
    }

    // Called from 0x399 handler. handsOnState = (0x399 data[5] >> 2) & 0x0F.
    void onNagSample(uint8_t handsOnState, unsigned long nowMs)
    {
        if (handsOnState != lastHandsOnState)
        {
            lastHandsOnState = handsOnState;
            if (handsOnState <= 2)
            {
                nagActive_ = false;
                injecting = false;    // C-1: NAG cleared → halt any in-progress burst immediately
                burstsThisCycle_ = 0; // hands back on → reset cycle
            }
        }
        if (handsOnState >= kNagThreshold)
        {
            nagActive_ = true;
            nagSamples_++;
            if (injecting)
                return; // current burst ongoing
            if (nowMs < cooldownUntilMs)
            {
                blockedCooldown_++; // instrumentation: blocked by stale cooldown
                return;
            }
            bool gapOk = (lastBurstMs == 0) || ((nowMs - lastBurstMs) > kBurstGapMs);
            if (!gapOk)
            {
                blockedGap_++; // instrumentation: within burst gap
                return;
            }
            if (burstsThisCycle_ >= kMaxBursts)
            {
                cooldownUntilMs = nowMs + kCooldownMs; // 3 bursts done → rest 3 s
                burstsThisCycle_ = 0;
                return;
            }
            // start a burst
            burstsStarted_++;
            injecting = true;
            strokeCount = 0;
            waveStartMs = nowMs;
            totalStrokes = (int)rng.range(kStrokesMin, kStrokesMax);
            int base = (handsOnState == 3) ? kAmpLight : kAmpHeavy;
            int jitter = (handsOnState == 3) ? kAmpLightJitter : kAmpHeavyJitter;
            amplitude = base + (int)rng.range(0, jitter);
            if (amplitude > kAmplitudeCap)
                amplitude = kAmplitudeCap;
            lastAmplitude_ = amplitude;
            strokeDurMs = (int)rng.range(kStrokeDurLo, kStrokeDurHi);
            direction = (rng.next() & 1u) ? 1 : -1;
            burstsThisCycle_++;
            lastBurstMs = nowMs;
        }
        else
        {
            nagActive_ = false;
            injecting = false; // C-1: NAG cleared → halt burst
        }
    }

    // Half-sine perturbation for current stroke. Returns 0 when burst done
    // (and clears injecting).
    int computeWave(unsigned long nowMs)
    {
        if (!injecting)
            return 0;
        unsigned long elapsed = nowMs - waveStartMs;
        if (elapsed <= (unsigned long)strokeDurMs)
        {
            float phase = ((float)elapsed / (float)strokeDurMs) * (float)M_PI;
            int w = (int)(sinf(phase) * (float)amplitude * (float)direction);
            if (w > kAmplitudeCap) w = kAmplitudeCap;
            if (w < -kAmplitudeCap) w = -kAmplitudeCap;
            return w;
        }
        // stroke finished → advance
        strokeCount++;
        if (strokeCount < totalStrokes)
        {
            direction *= -1;
            waveStartMs = nowMs;
        }
        else
        {
            injecting = false; // all strokes done
        }
        return 0; // stroke boundary frame: new stroke starts at sin(0)=0
    }

    // base decoded from data2Lo/data3 (caller passes frame bytes);
    // adds human_weight + pert, writes back. data[2] high nibble is caller-kept.
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int pert)
    {
        int torque = (int)(((uint16_t)data2Lo << 8) | data3);
        torque += (kHumanWeight + pert);
        data2Lo = (uint8_t)((torque >> 8) & 0x0F);
        data3 = (uint8_t)(torque & 0xFF);
    }
};
