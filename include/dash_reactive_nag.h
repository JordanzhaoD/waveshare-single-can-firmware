#pragma once

// dash_reactive_nag.h — Reactive NAG-suppression v2: 3-mode (IDLE/PROACTIVE/REACTIVE)
// + sustained DC-biased torque hold (integral > 0, vs v1 zero-mean sine).
// Port of public-source web_dnd_steer, Legacy-adapted + v2 optimizations (A+B+C).
//
// Modes (when active = toggle ON + APActive):
//   PROACTIVE  hos<=2 → periodic gentle sustained hold (every 2-5 s) to prevent NAG trigger
//   REACTIVE   hos>=3 → sustained strong hold (DC bias + jitter), fires at hos=3
//   IDLE       inactive
// NAG clear (hos<=2) fully resets reactive state + throttle (fixes v1 rec8 stale-cooldown).
//
// Pure logic: all time passed explicitly as nowMs (testable).

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

enum class NagMode
{
    IDLE,
    PROACTIVE,
    REACTIVE
};

// POD snapshot for /status + serial diag. No String (native-compilable).
struct DashReactiveDiag
{
    bool enabled{false};
    NagMode mode{NagMode::IDLE};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    int currentAmp{0};
    uint32_t nagSamples{0};
    uint32_t reactiveBursts{0};
    uint32_t proactiveWiggles{0};
    uint32_t echoSent{0};
    unsigned long nextProactiveInMs{0};
};

struct DashReactiveNagBurst
{
    // tunables
    static constexpr int kHumanWeight{8}; // baseline hands-present offset (added in applyToFrame)
    static constexpr int kNagThreshold{3};
    static constexpr int kReactiveAmp{70};    // REACTIVE DC bias magnitude
    static constexpr int kReactiveJitter{15}; // ± sine jitter on REACTIVE hold
    static constexpr int kProactiveAmp{35};   // PROACTIVE gentle DC bias
    static constexpr int kProactiveJitter{12};
    static constexpr int kAmplitudeCap{95}; // hard cap on |perturbation|
    static constexpr int kStrokesMin{2}, kStrokesMax{3};
    static constexpr int kStrokeDurLo{300}, kStrokeDurHi{400};                   // ms per stroke
    static constexpr unsigned long kReactiveGapMs{800};                          // intra-episode REACTIVE burst gap
    static constexpr unsigned long kProactiveIntLo{2000}, kProactiveIntHi{5000}; // PROACTIVE interval

    // runtime state
    DashReactivePRNG rng;
    NagMode mode_{NagMode::IDLE};
    bool injecting{false};
    bool proactiveBurst_{false}; // current burst is proactive (gentle) vs reactive (strong)
    unsigned long waveStartMs{0};
    int strokeCount{0};
    int totalStrokes{2};
    int strokeDurMs{350};
    int amp_{0};    // DC bias magnitude for current burst
    int jitter_{0}; // ± sine jitter amplitude for current burst
    unsigned long lastReactiveEndMs{0};
    unsigned long nextProactiveMs{0};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};
    // instrumentation
    uint32_t nagSamples_{0};
    uint32_t reactiveBursts_{0};
    uint32_t proactiveWiggles_{0};
    uint32_t echoSent_{0};

    void init(uint32_t seed)
    {
        rng.seed(seed);
        nextProactiveMs = 0;
    }
    void reset()
    {
        mode_ = NagMode::IDLE;
        injecting = false;
        proactiveBurst_ = false;
        strokeCount = 0;
        lastReactiveEndMs = 0;
        nextProactiveMs = 0;
        lastHandsOnState = 0;
        nagActive_ = false;
    }
    void resetCounters()
    {
        nagSamples_ = 0;
        reactiveBursts_ = 0;
        proactiveWiggles_ = 0;
        echoSent_ = 0;
    }
    void setCounters(uint32_t ns, uint32_t rb, uint32_t pw, uint32_t es)
    {
        nagSamples_ = ns;
        reactiveBursts_ = rb;
        proactiveWiggles_ = pw;
        echoSent_ = es;
    }
    void notifyEchoSent() { echoSent_++; }

    bool isNagActive() const { return nagActive_; }
    NagMode mode() const { return mode_; }
    bool shouldEcho(unsigned long /*nowMs*/) const { return injecting; }
    int amplitudeCap() const { return kAmplitudeCap; }
    int currentAmp() const { return injecting ? amp_ : 0; }
    uint32_t reactiveBursts() const { return reactiveBursts_; }
    uint32_t proactiveWiggles() const { return proactiveWiggles_; }
    unsigned long nextProactiveInMs(unsigned long nowMs) const
    {
        return (mode_ == NagMode::PROACTIVE && nextProactiveMs > nowMs) ? (nextProactiveMs - nowMs) : 0;
    }

    void startBurst(bool proactive, unsigned long nowMs)
    {
        injecting = true;
        proactiveBurst_ = proactive;
        strokeCount = 0;
        waveStartMs = nowMs;
        totalStrokes = (int)rng.range(kStrokesMin, kStrokesMax);
        strokeDurMs = (int)rng.range(kStrokeDurLo, kStrokeDurHi);
        if (proactive)
        {
            amp_ = kProactiveAmp;
            jitter_ = kProactiveJitter;
            proactiveWiggles_++;
        }
        else
        {
            amp_ = kReactiveAmp;
            jitter_ = kReactiveJitter;
            reactiveBursts_++;
        }
    }

    // Called per 0x399. hos = (0x399 data[5]>>2)&0x0F. active = toggle ON && APActive.
    void onNagSample(uint8_t hos, unsigned long nowMs, bool active)
    {
        if (hos != lastHandsOnState)
        {
            lastHandsOnState = hos;
            if (hos <= 2)
            {
                // NAG cleared → full reset of reactive state + throttle (fixes v1 rec8)
                nagActive_ = false;
                if (mode_ == NagMode::REACTIVE)
                {
                    injecting = false;
                    lastReactiveEndMs = 0;
                    mode_ = NagMode::PROACTIVE;
                    nextProactiveMs = nowMs; // schedule proactive soon
                }
            }
        }
        if (hos >= kNagThreshold)
        {
            nagActive_ = true;
            nagSamples_++;
            mode_ = NagMode::REACTIVE;
            if (!active) return;
            if (injecting && proactiveBurst_)
                injecting = false; // preempt ongoing proactive wiggle with reactive
            if (injecting) return; // reactive hold ongoing
            bool gapOk = (lastReactiveEndMs == 0) || ((nowMs - lastReactiveEndMs) > kReactiveGapMs);
            if (gapOk) startBurst(false, nowMs);
        }
        else
        {
            nagActive_ = false;
            if (mode_ == NagMode::REACTIVE) return; // wait for the hos-change block to transition
            mode_ = NagMode::PROACTIVE;
            if (!active) return;
            if (nextProactiveMs == 0) nextProactiveMs = nowMs;
            if (!injecting && nowMs >= nextProactiveMs) startBurst(true, nowMs);
        }
    }

    // DC-biased sustained hold, TIME-driven (real-ms pacing). Burst lasts
    // totalStrokes × strokeDurMs (~0.6-1.2 s) → a REAL sustained hold on the bus
    // (fixes v1 rec9 zero-mean + keeps v2's "driver grab" duration). Caller passes
    // real nowMs per 0x370 frame; elapsed = nowMs - waveStartMs drives stroke advance.
    // Every frame during the burst (incl. the terminal one) returns a POSITIVE DC
    // bias + sine jitter → integral > 0. The terminal frame clears injecting AFTER
    // returning its hold, so the burst ends on a positive frame (no zero leak).
    // NOTE: drain loops MUST advance nowMs (>strokeDurMs per step) to terminate.
    int computeHold(unsigned long nowMs)
    {
        if (!injecting) return 0;
        unsigned long elapsed = nowMs - waveStartMs;
        float phase = ((float)elapsed / (float)strokeDurMs) * (float)M_PI;
        if (phase > (float)M_PI) phase = (float)M_PI; // clamp past-stroke to stroke end
        int pert = amp_ + (int)(sinf(phase) * (float)jitter_);
        if (pert > kAmplitudeCap) pert = kAmplitudeCap;
        if (pert < 0) pert = 0;
        if (elapsed > (unsigned long)strokeDurMs)
        {
            strokeCount++;
            if (strokeCount < totalStrokes)
                waveStartMs = nowMs; // start next stroke
            else
            {
                injecting = false; // burst done (after this positive frame)
                if (proactiveBurst_)
                    nextProactiveMs = nowMs + rng.range(kProactiveIntLo, kProactiveIntHi);
                else
                    lastReactiveEndMs = nowMs;
            }
        }
        return pert;
    }

    // base decoded from data2Lo/data3; adds human_weight + pert. data[2] high nibble caller-kept.
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int pert)
    {
        int torque = (int)(((uint16_t)data2Lo << 8) | data3);
        torque += (kHumanWeight + pert);
        data2Lo = (uint8_t)((torque >> 8) & 0x0F);
        data3 = (uint8_t)(torque & 0xFF);
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        return {false, mode_, injecting, lastHandsOnState, currentAmp(),
                nagSamples_, reactiveBursts_, proactiveWiggles_, echoSent_,
                nextProactiveInMs(nowMs)};
    }
};
