#pragma once

// dash_reactive_nag.h — Human Torque Replay v3 reactive NAG state machine.
// Pure logic: all time is passed explicitly as nowMs (native-testable).

#include <cstdint>

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

enum class HumanReplayMode
{
    IDLE,
    REPLAYING,
    OBSERVING,
    COOLDOWN
};

// Backward-compatible name for older call sites that only store/print the mode.
using NagMode = HumanReplayMode;

enum class DashHumanReplayProfileId
{
    NONE,
    POS_MED,
    NEG_MED,
    POS_STRONG,
    NEG_STRONG
};

// POD snapshot for /status + serial diag. No String (native-compilable).
struct DashReactiveDiag
{
    bool enabled{false};
    HumanReplayMode mode{HumanReplayMode::IDLE};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    int currentAmp{0};
    uint32_t nagSamples{0};
    // Legacy field names kept for JSON/NVS compatibility. In v3 they map to
    // attempts/successes so old persistence slots can be reused safely.
    uint32_t reactiveBursts{0};
    uint32_t proactiveWiggles{0};
    uint32_t echoSent{0};
    unsigned long nextProactiveInMs{0};

    uint32_t replayAttempts{0};
    uint32_t replaySuccesses{0};
    uint32_t replayFailures{0};
    DashHumanReplayProfileId lastProfileId{DashHumanReplayProfileId::NONE};
    int lastProfileDir{0};
    int lastPeakRaw{0};
    int lastBaseRaw{0};
    int lastOutDeltaRaw{0};
    uint8_t profileIndex{0};
    uint8_t lastHosBefore{0};
    uint8_t lastHosAfter{0};
    unsigned long cooldownRemainMs{0};
    const char *blockedReason{""};
};

struct DashReactiveNagBurst
{
    // Hard safety bounds: keep these visible for safety-contract tests/review.
    static constexpr uint8_t kMaxAttempts{3};
    static constexpr unsigned long kCooldownMs{3000};
    static constexpr int kMaxDeltaRaw{180};
    static constexpr int kMaxSignedOutRaw{220};
    static constexpr uint8_t kProfileLen{10};

    static constexpr int16_t POS_MED[kProfileLen] = {40, 80, 110, 130, 145, 145, 130, 105, 75, 40};
    static constexpr int16_t NEG_MED[kProfileLen] = {-40, -80, -110, -130, -145, -145, -130, -105, -75, -40};
    static constexpr int16_t POS_STRONG[kProfileLen] = {50, 100, 140, 165, 175, 170, 150, 120, 80, 45};
    static constexpr int16_t NEG_STRONG[kProfileLen] = {-50, -100, -140, -165, -175, -170, -150, -120, -80, -45};

    DashReactivePRNG rng;

    HumanReplayMode mode_{HumanReplayMode::IDLE};
    bool preferPositive_{true};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};

    uint32_t nagSamples_{0};
    uint32_t replayAttempts_{0};
    uint32_t replaySuccesses_{0};
    uint32_t replayFailures_{0};
    uint32_t echoSent_{0};
    uint8_t episodeAttempts_{0};

    DashHumanReplayProfileId lastProfileId_{DashHumanReplayProfileId::NONE};
    int lastProfileDir_{0};
    int lastPeakRaw_{0};
    int lastBaseRaw_{0};
    int lastOutDeltaRaw_{0};
    uint8_t profileIndex_{0};
    uint8_t lastHosBefore_{0};
    uint8_t lastHosAfter_{0};
    unsigned long cooldownStartMs_{0};
    const char *blockedReason_{""};
    uint8_t observeSamples_{0};
    uint8_t attemptEchoSent_{0};

    void init(uint32_t seed)
    {
        rng.seed(seed);
        preferPositive_ = (rng.next() & 1u) == 0;
    }

    void reset()
    {
        mode_ = HumanReplayMode::IDLE;
        injecting = false;
        lastHandsOnState = 0;
        nagActive_ = false;
        lastProfileId_ = DashHumanReplayProfileId::NONE;
        lastProfileDir_ = 0;
        lastPeakRaw_ = 0;
        lastBaseRaw_ = 0;
        lastOutDeltaRaw_ = 0;
        profileIndex_ = 0;
        episodeAttempts_ = 0;
        lastHosBefore_ = 0;
        lastHosAfter_ = 0;
        cooldownStartMs_ = 0;
        blockedReason_ = "";
        observeSamples_ = 0;
        attemptEchoSent_ = 0;
    }

    void resetCounters()
    {
        nagSamples_ = 0;
        replayAttempts_ = 0;
        replaySuccesses_ = 0;
        replayFailures_ = 0;
        echoSent_ = 0;
        episodeAttempts_ = 0;
        attemptEchoSent_ = 0;
    }

    // Diagnostic test hook: add distinct magic amounts so persistence across
    // power-cycle is unambiguous (111/222/333/444 after one bump).
    void bumpCounters()
    {
        nagSamples_ += 111;
        replayAttempts_ += 222;
        replaySuccesses_ += 333;
        echoSent_ += 444;
    }

    // Reuse old NVS slots: ns=nagSamples, attempts=old reactiveBursts,
    // successes=old proactiveWiggles, es=echoSent.
    void setCounters(uint32_t ns, uint32_t attempts, uint32_t successes, uint32_t es)
    {
        nagSamples_ = ns;
        replayAttempts_ = attempts;
        replaySuccesses_ = successes;
        echoSent_ = es;
        replayFailures_ = 0;
        episodeAttempts_ = 0;
    }

    void notifyEchoSent()
    {
        echoSent_++;
        if (attemptEchoSent_ < 0xFF) attemptEchoSent_++;
    }
    void noteBaseTorqueRaw(int raw) { lastBaseRaw_ = raw; }

    bool isNagActive() const { return nagActive_; }
    HumanReplayMode mode() const { return mode_; }
    bool shouldEcho(unsigned long /*nowMs*/) const { return mode_ == HumanReplayMode::REPLAYING && profileIndex_ < kProfileLen; }
    int amplitudeCap() const { return kMaxDeltaRaw; }
    int currentAmp() const { return shouldEcho(0) ? lastPeakRaw_ : 0; }
    uint32_t nagSamples() const { return nagSamples_; }
    uint32_t replayAttempts() const { return replayAttempts_; }
    uint32_t replaySuccesses() const { return replaySuccesses_; }
    uint32_t replayFailures() const { return replayFailures_; }
    uint32_t reactiveBursts() const { return replayAttempts_; }
    uint32_t proactiveWiggles() const { return replaySuccesses_; }
    DashHumanReplayProfileId lastProfileId() const { return lastProfileId_; }
    int lastProfileDir() const { return lastProfileDir_; }
    int lastPeakRaw() const { return lastPeakRaw_; }
    int lastOutDeltaRaw() const { return lastOutDeltaRaw_; }
    const char *blockedReason() const { return blockedReason_; }
    uint8_t lastHosBefore() const { return lastHosBefore_; }
    uint8_t lastHosAfter() const { return lastHosAfter_; }
    unsigned long cooldownRemainMs(unsigned long nowMs) const
    {
        if (mode_ != HumanReplayMode::COOLDOWN) return 0;
        unsigned long elapsed = nowMs - cooldownStartMs_;
        return (elapsed >= kCooldownMs) ? 0 : (kCooldownMs - elapsed);
    }
    unsigned long nextProactiveInMs(unsigned long nowMs) const { return cooldownRemainMs(nowMs); }

    static int clampInt(int value, int lo, int hi)
    {
        if (value < lo) return lo;
        if (value > hi) return hi;
        return value;
    }

    static int decodeSignedTorque(uint8_t data2Lo, uint8_t data3)
    {
        int raw = ((int)(data2Lo & 0x0F) << 8) | data3;
        return raw - 0x800;
    }

    static uint16_t encodeSignedTorque(int signedRaw)
    {
        int bounded = clampInt(signedRaw, -2048, 2047) + 0x800;
        return (uint16_t)(bounded & 0x0FFF);
    }

    void applyDeltaToFrame(uint8_t &data2Lo, uint8_t &data3, int deltaRaw)
    {
        int delta = clampInt(deltaRaw, -kMaxDeltaRaw, kMaxDeltaRaw);
        int out = clampInt(decodeSignedTorque(data2Lo, data3) + delta, -kMaxSignedOutRaw, kMaxSignedOutRaw);
        uint16_t encoded = encodeSignedTorque(out);
        data2Lo = (uint8_t)((encoded >> 8) & 0x0F);
        data3 = (uint8_t)(encoded & 0xFF);
    }

    const int16_t *currentProfile() const
    {
        switch (lastProfileId_)
        {
        case DashHumanReplayProfileId::POS_MED:
            return POS_MED;
        case DashHumanReplayProfileId::NEG_MED:
            return NEG_MED;
        case DashHumanReplayProfileId::POS_STRONG:
            return POS_STRONG;
        case DashHumanReplayProfileId::NEG_STRONG:
            return NEG_STRONG;
        case DashHumanReplayProfileId::NONE:
        default:
            return POS_MED;
        }
    }

    static int peakAbs(const int16_t *profile)
    {
        int peak = 0;
        for (uint8_t i = 0; i < kProfileLen; ++i)
        {
            int v = profile[i] < 0 ? -profile[i] : profile[i];
            if (v > peak) peak = v;
        }
        return peak;
    }

    void cancel(const char *reason)
    {
        mode_ = HumanReplayMode::IDLE;
        injecting = false;
        profileIndex_ = kProfileLen;
        observeSamples_ = 0;
        attemptEchoSent_ = 0;
        blockedReason_ = reason ? reason : "";
    }

    void startReplay(unsigned long nowMs)
    {
        if (episodeAttempts_ >= kMaxAttempts)
        {
            enterCooldown(nowMs);
            return;
        }

        const uint8_t nextAttempt = static_cast<uint8_t>(episodeAttempts_ + 1);
        const bool strong = nextAttempt >= kMaxAttempts;
        bool positive = preferPositive_;
        if (nextAttempt == 2) positive = !preferPositive_;

        if (strong)
            lastProfileId_ = positive ? DashHumanReplayProfileId::POS_STRONG : DashHumanReplayProfileId::NEG_STRONG;
        else
            lastProfileId_ = positive ? DashHumanReplayProfileId::POS_MED : DashHumanReplayProfileId::NEG_MED;

        lastProfileDir_ = positive ? 1 : -1;
        lastPeakRaw_ = peakAbs(currentProfile());
        lastOutDeltaRaw_ = 0;
        profileIndex_ = 0;
        observeSamples_ = 0;
        attemptEchoSent_ = 0;
        episodeAttempts_ = nextAttempt;
        replayAttempts_++;
        mode_ = HumanReplayMode::REPLAYING;
        injecting = true;
        blockedReason_ = "";
    }

    void enterCooldown(unsigned long nowMs)
    {
        mode_ = HumanReplayMode::COOLDOWN;
        injecting = false;
        profileIndex_ = kProfileLen;
        cooldownStartMs_ = nowMs;
        blockedReason_ = "maxAttempts";
        replayFailures_++;
    }

    void failReplayTx(unsigned long nowMs)
    {
        mode_ = HumanReplayMode::COOLDOWN;
        injecting = false;
        profileIndex_ = kProfileLen;
        cooldownStartMs_ = nowMs;
        blockedReason_ = "txFail";
        observeSamples_ = 0;
        attemptEchoSent_ = 0;
        replayFailures_++;
    }

    int peekReplayDelta(unsigned long /*nowMs*/) const
    {
        if (!shouldEcho(0)) return 0;
        return currentProfile()[profileIndex_];
    }

    void commitReplayDelta(int deltaRaw)
    {
        if (!shouldEcho(0)) return;
        lastOutDeltaRaw_ = deltaRaw;
        profileIndex_++;
        if (profileIndex_ >= kProfileLen)
        {
            mode_ = HumanReplayMode::OBSERVING;
            injecting = false;
            observeSamples_ = 0;
        }
    }

    int nextReplayDelta(unsigned long nowMs)
    {
        int delta = peekReplayDelta(nowMs);
        commitReplayDelta(delta);
        return delta;
    }

    // Backward-compatible name used by LegacyHandler's 0x370 echo path.
    int computeHold(unsigned long nowMs) { return nextReplayDelta(nowMs); }

    // Backward-compatible wrapper: v3 treats the argument as the replay delta.
    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int pert) { applyDeltaToFrame(data2Lo, data3, pert); }

    // Called per 0x399. hos = (0x399 data[5]>>2)&0x0F. active = toggle ON && APActive.
    void onNagSample(uint8_t hos, unsigned long nowMs, bool active)
    {
        lastHandsOnState = hos;

        if (hos <= 2)
        {
            nagActive_ = false;
            if ((mode_ == HumanReplayMode::REPLAYING || mode_ == HumanReplayMode::OBSERVING) && attemptEchoSent_ > 0)
            {
                replaySuccesses_++;
                lastHosAfter_ = hos;
            }
            mode_ = HumanReplayMode::IDLE;
            injecting = false;
            profileIndex_ = kProfileLen;
            episodeAttempts_ = 0;
            observeSamples_ = 0;
            attemptEchoSent_ = 0;
            blockedReason_ = "";
            return;
        }

        if (hos < 3) return;

        nagActive_ = true;
        nagSamples_++;
        lastHosBefore_ = hos;

        if (mode_ == HumanReplayMode::COOLDOWN)
        {
            unsigned long elapsed = nowMs - cooldownStartMs_;
            if (elapsed < kCooldownMs)
            {
                blockedReason_ = "maxAttempts";
                return;
            }
            mode_ = HumanReplayMode::IDLE;
            injecting = false;
            profileIndex_ = kProfileLen;
            episodeAttempts_ = 0;
            observeSamples_ = 0;
            blockedReason_ = "";
        }

        if (!active)
        {
            cancel("toggle");
            return;
        }

        if (mode_ == HumanReplayMode::REPLAYING) return;

        if (mode_ == HumanReplayMode::OBSERVING)
        {
            observeSamples_++;
            if (observeSamples_ < 2) return;
            if (episodeAttempts_ >= kMaxAttempts)
            {
                enterCooldown(nowMs);
                return;
            }
        }

        startReplay(nowMs);
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        DashReactiveDiag d;
        d.mode = mode_;
        d.injecting = injecting;
        d.lastHandsOnState = lastHandsOnState;
        d.currentAmp = currentAmp();
        d.nagSamples = nagSamples_;
        d.reactiveBursts = replayAttempts_;
        d.proactiveWiggles = replaySuccesses_;
        d.echoSent = echoSent_;
        d.nextProactiveInMs = nextProactiveInMs(nowMs);
        d.replayAttempts = replayAttempts_;
        d.replaySuccesses = replaySuccesses_;
        d.replayFailures = replayFailures_;
        d.lastProfileId = lastProfileId_;
        d.lastProfileDir = lastProfileDir_;
        d.lastPeakRaw = lastPeakRaw_;
        d.lastBaseRaw = lastBaseRaw_;
        d.lastOutDeltaRaw = lastOutDeltaRaw_;
        d.profileIndex = profileIndex_;
        d.lastHosBefore = lastHosBefore_;
        d.lastHosAfter = lastHosAfter_;
        d.cooldownRemainMs = cooldownRemainMs(nowMs);
        d.blockedReason = blockedReason_;
        return d;
    }
};
