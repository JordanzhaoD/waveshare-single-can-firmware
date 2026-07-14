#pragma once

// Pure Reactive v2 sustained-hold state machine.
// It owns no CAN routing or hardware I/O; callers provide HOS/AP samples and
// monotonic uint32_t milliseconds, then consume the bounded positive hold.

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class DashReactiveHoldPhase : uint8_t
{
    Idle,
    Proactive,
    Reactive
};

struct DashReactiveHoldDiag
{
    DashReactiveHoldPhase phase{DashReactiveHoldPhase::Idle};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    int currentAmp{0};
    uint32_t nagSamples{0};
    uint32_t reactiveBursts{0};
    uint32_t proactiveWiggles{0};
    uint32_t echoSent{0};
    uint32_t nextProactiveInMs{0};
};

class DashReactiveHoldNag
{
public:
    void init(uint32_t seed)
    {
        rng_.seed(seed);
        clearTransientState();
    }

    void reset()
    {
        clearTransientState();
    }

    void resetCounters()
    {
        nagSamples_ = 0;
        reactiveBursts_ = 0;
        proactiveWiggles_ = 0;
        echoSent_ = 0;
        countersDirty_ = true;
    }

    void bumpCounters()
    {
        nagSamples_ += 111;
        reactiveBursts_ += 222;
        proactiveWiggles_ += 333;
        echoSent_ += 444;
        countersDirty_ = true;
    }

    void setCounters(uint32_t nagSamples,
                     uint32_t reactiveBursts,
                     uint32_t proactiveWiggles,
                     uint32_t echoSent)
    {
        nagSamples_ = nagSamples;
        reactiveBursts_ = reactiveBursts;
        proactiveWiggles_ = proactiveWiggles;
        echoSent_ = echoSent;
        countersDirty_ = false;
    }

    bool countersDirty() const { return countersDirty_; }
    void markCountersPersisted() { countersDirty_ = false; }

    void onNagSample(uint8_t hos, uint32_t nowMs, bool active)
    {
        retireExpiredBurst(nowMs);
        lastHandsOnState_ = hos;

        if (hos >= kNagThreshold)
        {
            nagSamples_++;
            countersDirty_ = true;
            phase_ = DashReactiveHoldPhase::Reactive;
            haveNextProactiveMs_ = false;

            if (!active)
                return;

            if (injecting_)
            {
                if (!proactiveBurst_)
                    return;
                injecting_ = false;
                proactiveBurst_ = false;
            }

            if (!reactiveCooldownComplete(nowMs))
                return;

            startBurst(false, nowMs);
            return;
        }

        const bool leavingReactive = phase_ == DashReactiveHoldPhase::Reactive;
        if (leavingReactive || (injecting_ && !proactiveBurst_))
        {
            injecting_ = false;
            proactiveBurst_ = false;
            haveLastReactiveEndMs_ = false;
            haveNextProactiveMs_ = true;
            nextProactiveMs_ = nowMs;
        }

        phase_ = DashReactiveHoldPhase::Proactive;
        if (!active)
            return;

        if (injecting_)
            return;

        if (!haveNextProactiveMs_)
        {
            haveNextProactiveMs_ = true;
            nextProactiveMs_ = nowMs;
        }

        if (timeReached(nowMs, nextProactiveMs_))
            startBurst(true, nowMs);
    }

    bool shouldEcho(uint32_t nowMs) const
    {
        return injecting_ && !burstExpired(nowMs);
    }

    int computeHold(uint32_t nowMs)
    {
        if (!injecting_)
            return 0;

        if (burstExpired(nowMs))
        {
            finishBurst(waveStartMs_ + remainingBurstDurationMs());
            return 0;
        }

        uint32_t elapsedToProcess = nowMs - waveStartMs_;
        while (elapsedToProcess >= strokeDurationMs_ &&
               strokeCount_ + 1 < totalStrokes_)
        {
            strokeCount_++;
            waveStartMs_ += strokeDurationMs_;
            elapsedToProcess = nowMs - waveStartMs_;
        }

        const uint32_t elapsed = nowMs - waveStartMs_;
        const float normalized =
            static_cast<float>(elapsed) /
            static_cast<float>(strokeDurationMs_);
        const float phase = std::min(1.0f, normalized) * kPi;
        const int hold = clampHold(
            amp_ + static_cast<int>(std::sin(phase) * jitter_));

        if (elapsed >= strokeDurationMs_)
            finishBurst(waveStartMs_ + strokeDurationMs_);

        return hold;
    }

    void applyToFrame(uint8_t &data2LowNibble,
                      uint8_t &data3,
                      int hold)
    {
        int torque = (static_cast<int>(data2LowNibble & 0x0F) << 8) |
                     static_cast<int>(data3);
        torque = std::min(0x0FFF, torque + kHumanWeight + clampHold(hold));
        data2LowNibble = static_cast<uint8_t>((torque >> 8) & 0x0F);
        data3 = static_cast<uint8_t>(torque & 0xFF);
    }

    void notifyEchoSent()
    {
        echoSent_++;
        countersDirty_ = true;
    }

    DashReactiveHoldDiag diag(uint32_t nowMs) const
    {
        const bool activeHold = shouldEcho(nowMs);
        return {phase_,
                activeHold,
                lastHandsOnState_,
                activeHold ? amp_ : 0,
                nagSamples_,
                reactiveBursts_,
                proactiveWiggles_,
                echoSent_,
                proactiveRemainingMs(nowMs)};
    }

private:
    class Prng
    {
    public:
        void seed(uint32_t value)
        {
            state_ = value ? value : kDefaultSeed;
        }

        uint32_t range(uint32_t low, uint32_t high)
        {
            if (high < low)
                return low;
            return low + (next() % (high - low + 1u));
        }

    private:
        uint32_t next()
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 17;
            state_ ^= state_ << 5;
            return state_;
        }

        static constexpr uint32_t kDefaultSeed = 0xDEADBEEFu;
        uint32_t state_{kDefaultSeed};
    };

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr uint8_t kNagThreshold = 3;
    static constexpr int kHumanWeight = 8;
    static constexpr int kReactiveAmp = 70;
    static constexpr int kReactiveJitter = 15;
    static constexpr int kProactiveAmp = 35;
    static constexpr int kProactiveJitter = 12;
    static constexpr int kMinStrokes = 2;
    static constexpr int kMaxStrokes = 3;
    static constexpr uint32_t kMinStrokeDurationMs = 300;
    static constexpr uint32_t kMaxStrokeDurationMs = 400;
    static constexpr uint32_t kReactiveCooldownMs = 800;
    static constexpr uint32_t kProactiveIntervalMinMs = 2000;
    static constexpr uint32_t kProactiveIntervalMaxMs = 5000;

    static int clampHold(int value)
    {
        return std::max(0, std::min(95, value));
    }

    static bool timeReached(uint32_t nowMs, uint32_t deadlineMs)
    {
        return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
    }

    uint32_t remainingBurstDurationMs() const
    {
        const int remainingStrokes = totalStrokes_ - strokeCount_;
        return remainingStrokes > 0
                   ? static_cast<uint32_t>(remainingStrokes) * strokeDurationMs_
                   : 0;
    }

    bool burstExpired(uint32_t nowMs) const
    {
        return injecting_ &&
               (nowMs - waveStartMs_) > remainingBurstDurationMs();
    }

    void finishBurst(uint32_t endMs)
    {
        injecting_ = false;
        if (proactiveBurst_)
        {
            haveNextProactiveMs_ = true;
            nextProactiveMs_ = endMs + rng_.range(kProactiveIntervalMinMs,
                                                  kProactiveIntervalMaxMs);
        }
        else
        {
            haveLastReactiveEndMs_ = true;
            lastReactiveEndMs_ = endMs;
        }
        proactiveBurst_ = false;
    }

    void retireExpiredBurst(uint32_t nowMs)
    {
        if (burstExpired(nowMs))
            finishBurst(waveStartMs_ + remainingBurstDurationMs());
    }

    bool reactiveCooldownComplete(uint32_t nowMs) const
    {
        return !haveLastReactiveEndMs_ ||
               (nowMs - lastReactiveEndMs_) >= kReactiveCooldownMs;
    }

    uint32_t proactiveRemainingMs(uint32_t nowMs) const
    {
        if (phase_ != DashReactiveHoldPhase::Proactive ||
            !haveNextProactiveMs_ ||
            timeReached(nowMs, nextProactiveMs_))
            return 0;
        return nextProactiveMs_ - nowMs;
    }

    void startBurst(bool proactive, uint32_t nowMs)
    {
        injecting_ = true;
        proactiveBurst_ = proactive;
        waveStartMs_ = nowMs;
        strokeCount_ = 0;
        totalStrokes_ = static_cast<int>(rng_.range(kMinStrokes, kMaxStrokes));
        strokeDurationMs_ = rng_.range(kMinStrokeDurationMs, kMaxStrokeDurationMs);
        haveNextProactiveMs_ = false;

        if (proactive)
        {
            amp_ = kProactiveAmp;
            jitter_ = kProactiveJitter;
            proactiveWiggles_++;
            countersDirty_ = true;
        }
        else
        {
            amp_ = kReactiveAmp;
            jitter_ = kReactiveJitter;
            reactiveBursts_++;
            countersDirty_ = true;
        }
    }

    void clearTransientState()
    {
        phase_ = DashReactiveHoldPhase::Idle;
        injecting_ = false;
        proactiveBurst_ = false;
        waveStartMs_ = 0;
        strokeCount_ = 0;
        totalStrokes_ = kMinStrokes;
        strokeDurationMs_ = kMinStrokeDurationMs;
        amp_ = 0;
        jitter_ = 0;
        haveLastReactiveEndMs_ = false;
        lastReactiveEndMs_ = 0;
        haveNextProactiveMs_ = false;
        nextProactiveMs_ = 0;
        lastHandsOnState_ = 0;
    }

    Prng rng_{};
    DashReactiveHoldPhase phase_{DashReactiveHoldPhase::Idle};
    bool injecting_{false};
    bool proactiveBurst_{false};
    uint32_t waveStartMs_{0};
    int strokeCount_{0};
    int totalStrokes_{kMinStrokes};
    uint32_t strokeDurationMs_{kMinStrokeDurationMs};
    int amp_{0};
    int jitter_{0};
    bool haveLastReactiveEndMs_{false};
    uint32_t lastReactiveEndMs_{0};
    bool haveNextProactiveMs_{false};
    uint32_t nextProactiveMs_{0};
    uint8_t lastHandsOnState_{0};
    uint32_t nagSamples_{0};
    uint32_t reactiveBursts_{0};
    uint32_t proactiveWiggles_{0};
    uint32_t echoSent_{0};
    bool countersDirty_{false};
};
