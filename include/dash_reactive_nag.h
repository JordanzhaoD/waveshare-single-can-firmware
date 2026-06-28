#pragma once

// dash_reactive_nag.h — Human Torque Replay TSL6P v4 reactive NAG state machine.
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
    BURST_ON,
    REPLAYING = BURST_ON,
    BURST_OFF,
    OBSERVING = BURST_OFF,
    COOLDOWN
};

// Backward-compatible name for older call sites that only store/print the mode.
using NagMode = HumanReplayMode;

enum class DashHumanReplayProfileId
{
    NONE,
    TSL6P,
    POS_MED = TSL6P,
    NEG_MED = TSL6P,
    POS_STRONG = TSL6P,
    NEG_STRONG = TSL6P
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

    uint32_t burstSessions{0};
    uint32_t burstOnEntries{0};
    uint32_t burstOffEntries{0};
    uint32_t burstFramesSent{0};
    uint32_t burstCyclesCompleted{0};
    uint32_t hosClearEvents{0};
    uint32_t hosClearDuringOn{0};
    uint32_t hosClearDuringOff{0};
    uint32_t hosClearWhileIdle{0};
    uint32_t hosClearWhileCooldown{0};
    uint32_t abortBlocks{0};
    uint32_t gateBlocks{0};
    uint32_t txFailures{0};
    uint8_t lastApState{0};
    unsigned long phaseRemainMs{0};
    int lastTorqueRaw{0};
    int lastTorqueNmX100{0};
};

struct DashReactiveNagBurst
{
    // Hard safety bounds: keep these visible for safety-contract tests/review.
    static constexpr unsigned long kBurstOnMs{1000};
    static constexpr unsigned long kBurstOffMs{1500};
    static constexpr unsigned long kAbortCooldownMs{3000};
    static constexpr unsigned long kTxFailCooldownMs{3000};
    static constexpr int kMaxSignedOutRaw{180};
    static constexpr uint8_t kTorqueSequenceLen{4};
    static constexpr int16_t TSL6P_TORQUE_RAW[kTorqueSequenceLen] = {180, 150, -150, -180};

    DashReactivePRNG rng;

    HumanReplayMode mode_{HumanReplayMode::IDLE};
    bool injecting{false};
    uint8_t lastHandsOnState{0};
    bool nagActive_{false};

    uint32_t nagSamples_{0};
    uint32_t burstSessions_{0};
    uint32_t burstOnEntries_{0};
    uint32_t burstOffEntries_{0};
    uint32_t burstFramesSent_{0};
    uint32_t burstCyclesCompleted_{0};
    uint32_t hosClearEvents_{0};
    uint32_t hosClearDuringOn_{0};
    uint32_t hosClearDuringOff_{0};
    uint32_t hosClearWhileIdle_{0};
    uint32_t hosClearWhileCooldown_{0};
    uint32_t abortBlocks_{0};
    uint32_t gateBlocks_{0};
    uint32_t txFailures_{0};

    unsigned long phaseStartMs_{0};
    unsigned long cooldownStartMs_{0};
    unsigned long cooldownDurationMs_{0};
    const char *blockedReason_{""};
    uint8_t torqueIndex_{0};
    uint8_t lastHosBefore_{0};
    uint8_t lastHosAfter_{0};
    uint8_t lastApState_{0};
    int lastBaseRaw_{0};
    int lastTorqueRaw_{0};
    int lastOutDeltaRaw_{0};

    void init(uint32_t seed) { rng.seed(seed); }

    void reset()
    {
        mode_ = HumanReplayMode::IDLE;
        injecting = false;
        lastHandsOnState = 0;
        nagActive_ = false;
        phaseStartMs_ = 0;
        cooldownStartMs_ = 0;
        cooldownDurationMs_ = 0;
        blockedReason_ = "";
        torqueIndex_ = 0;
        lastHosBefore_ = 0;
        lastHosAfter_ = 0;
        lastApState_ = 0;
        lastBaseRaw_ = 0;
        lastTorqueRaw_ = 0;
        lastOutDeltaRaw_ = 0;
    }

    void resetCounters()
    {
        nagSamples_ = 0;
        burstSessions_ = 0;
        burstOnEntries_ = 0;
        burstOffEntries_ = 0;
        burstFramesSent_ = 0;
        burstCyclesCompleted_ = 0;
        hosClearEvents_ = 0;
        hosClearDuringOn_ = 0;
        hosClearDuringOff_ = 0;
        hosClearWhileIdle_ = 0;
        hosClearWhileCooldown_ = 0;
        abortBlocks_ = 0;
        gateBlocks_ = 0;
        txFailures_ = 0;
    }

    // Diagnostic test hook: add distinct magic amounts so persistence across
    // power-cycle is unambiguous (111/222/333/444 after one bump).
    void bumpCounters()
    {
        nagSamples_ += 111;
        burstSessions_ += 222;
        hosClearEvents_ += 333;
        burstFramesSent_ += 444;
    }

    void setCounters(uint32_t ns, uint32_t sessions, uint32_t clears, uint32_t sent)
    {
        nagSamples_ = ns;
        burstSessions_ = sessions;
        hosClearEvents_ = clears;
        burstFramesSent_ = sent;
        burstOnEntries_ = sessions;
        burstOffEntries_ = 0;
        burstCyclesCompleted_ = 0;
        hosClearDuringOn_ = 0;
        hosClearDuringOff_ = 0;
        hosClearWhileIdle_ = 0;
        hosClearWhileCooldown_ = 0;
        abortBlocks_ = 0;
        gateBlocks_ = 0;
        txFailures_ = 0;
    }

    void notifyEchoSent()
    {
        burstFramesSent_++;
    }

    void noteBaseTorqueRaw(int raw) { lastBaseRaw_ = raw; }

    bool isNagActive() const { return nagActive_; }
    HumanReplayMode mode() const { return mode_; }
    bool shouldEcho(unsigned long nowMs) const
    {
        return mode_ == HumanReplayMode::BURST_ON && (nowMs - phaseStartMs_) < kBurstOnMs;
    }

    void advance(unsigned long nowMs, bool gatesActive = true, const char *gateReason = "toggle")
    {
        if (!gatesActive)
        {
            if (nagActive_ || mode_ == HumanReplayMode::BURST_ON || mode_ == HumanReplayMode::BURST_OFF)
                cancel(gateReason);
            return;
        }
        if (!nagActive_) return;

        // Advance against scheduled boundaries, not the arrival time of the next
        // 0x399 sample. This keeps the 1000ms ON / 1500ms OFF cadence stable
        // even when samples are delayed and lets 0x370 diagnostics observe the
        // correct phase without stretching OFF.
        while (true)
        {
            if (mode_ == HumanReplayMode::BURST_ON)
            {
                unsigned long boundary = phaseStartMs_ + kBurstOnMs;
                if ((nowMs - boundary) < 0x80000000UL)
                    enterBurstOff(boundary);
                else
                    return;
            }
            else if (mode_ == HumanReplayMode::BURST_OFF)
            {
                unsigned long boundary = phaseStartMs_ + kBurstOffMs;
                if ((nowMs - boundary) < 0x80000000UL)
                    enterBurstOn(boundary);
                else
                    return;
            }
            else
            {
                return;
            }
        }
    }

    HumanReplayMode classifyHosClearPhase(unsigned long nowMs)
    {
        if (mode_ == HumanReplayMode::BURST_ON)
        {
            unsigned long boundary = phaseStartMs_ + kBurstOnMs;
            if ((nowMs - boundary) < 0x80000000UL)
                enterBurstOff(boundary);
        }
        if (mode_ == HumanReplayMode::BURST_OFF)
        {
            // HOS<=2 at or just after the OFF boundary is conservatively
            // attributed to the prior rest window. Do not start a new ON phase
            // unless a later HOS>2 sample proves the nag persisted.
            return HumanReplayMode::BURST_OFF;
        }
        return mode_;
    }

    void retireExpiredCooldown(unsigned long nowMs)
    {
        if (mode_ == HumanReplayMode::COOLDOWN && cooldownRemainMs(nowMs) == 0)
        {
            mode_ = HumanReplayMode::IDLE;
            injecting = false;
            blockedReason_ = "";
        }
    }
    int amplitudeCap() const { return kMaxSignedOutRaw; }
    int currentAmp(unsigned long nowMs) const { return shouldEcho(nowMs) ? (lastTorqueRaw_ < 0 ? -lastTorqueRaw_ : lastTorqueRaw_) : 0; }
    int currentAmp() const { return currentAmp(phaseStartMs_); }
    uint32_t nagSamples() const { return nagSamples_; }
    uint32_t burstSessions() const { return burstSessions_; }
    uint32_t burstOnEntries() const { return burstOnEntries_; }
    uint32_t burstOffEntries() const { return burstOffEntries_; }
    uint32_t burstFramesSent() const { return burstFramesSent_; }
    uint32_t burstCyclesCompleted() const { return burstCyclesCompleted_; }
    uint32_t hosClearEvents() const { return hosClearEvents_; }
    uint32_t hosClearDuringOn() const { return hosClearDuringOn_; }
    uint32_t hosClearDuringOff() const { return hosClearDuringOff_; }
    uint32_t hosClearWhileIdle() const { return hosClearWhileIdle_; }
    uint32_t hosClearWhileCooldown() const { return hosClearWhileCooldown_; }
    uint32_t abortBlocks() const { return abortBlocks_; }
    uint32_t gateBlocks() const { return gateBlocks_; }
    uint32_t txFailures() const { return txFailures_; }

    uint32_t replayAttempts() const { return burstSessions_; }
    uint32_t replaySuccesses() const { return hosClearEvents_; }
    uint32_t replayFailures() const { return txFailures_ + abortBlocks_; }
    uint32_t reactiveBursts() const { return burstSessions_; }
    uint32_t proactiveWiggles() const { return hosClearEvents_; }
    uint32_t echoSent() const { return burstFramesSent_; }
    DashHumanReplayProfileId lastProfileId() const { return burstSessions_ ? DashHumanReplayProfileId::TSL6P : DashHumanReplayProfileId::NONE; }
    int lastProfileDir() const { return lastTorqueRaw_ < 0 ? -1 : (lastTorqueRaw_ > 0 ? 1 : 0); }
    int lastPeakRaw() const { return kMaxSignedOutRaw; }
    int lastBaseRaw() const { return lastBaseRaw_; }
    int lastOutDeltaRaw() const { return lastOutDeltaRaw_; }
    int lastTorqueRaw() const { return lastTorqueRaw_; }
    int lastTorqueNmX100() const { return lastTorqueRaw_; }
    uint8_t profileIndex() const { return torqueIndex_; }
    const char *blockedReason() const { return blockedReason_; }
    uint8_t lastHosBefore() const { return lastHosBefore_; }
    uint8_t lastHosAfter() const { return lastHosAfter_; }
    uint8_t lastApState() const { return lastApState_; }

    unsigned long cooldownRemainMs(unsigned long nowMs) const
    {
        if (mode_ != HumanReplayMode::COOLDOWN) return 0;
        unsigned long elapsed = nowMs - cooldownStartMs_;
        return (elapsed >= cooldownDurationMs_) ? 0 : (cooldownDurationMs_ - elapsed);
    }

    unsigned long phaseRemainMs(unsigned long nowMs) const
    {
        if (mode_ == HumanReplayMode::BURST_ON)
        {
            unsigned long elapsed = nowMs - phaseStartMs_;
            return (elapsed >= kBurstOnMs) ? 0 : (kBurstOnMs - elapsed);
        }
        if (mode_ == HumanReplayMode::BURST_OFF)
        {
            unsigned long elapsed = nowMs - phaseStartMs_;
            return (elapsed >= kBurstOffMs) ? 0 : (kBurstOffMs - elapsed);
        }
        return cooldownRemainMs(nowMs);
    }

    unsigned long nextProactiveInMs(unsigned long nowMs) const { return phaseRemainMs(nowMs); }

    static int clampInt(int value, int lo, int hi)
    {
        if (value < lo) return lo;
        if (value > hi) return hi;
        return value;
    }

    static bool reasonEquals(const char *lhs, const char *rhs)
    {
        if (!lhs || !rhs) return lhs == rhs;
        while (*lhs && *rhs)
        {
            if (*lhs != *rhs) return false;
            ++lhs;
            ++rhs;
        }
        return *lhs == *rhs;
    }

    static bool isAbortState(uint8_t apState) { return apState == 8 || apState == 9; }

    bool cooldownActive(unsigned long nowMs) const { return mode_ == HumanReplayMode::COOLDOWN && cooldownRemainMs(nowMs) > 0; }
    bool txFailCooldownActive(unsigned long nowMs) const { return cooldownActive(nowMs) && reasonEquals(blockedReason_, "txFail"); }
    bool abortCooldownActive(unsigned long nowMs) const { return cooldownActive(nowMs) && reasonEquals(blockedReason_, "abort"); }

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

    static void setSignedTorqueInFrame(uint8_t &data2Lo, uint8_t &data3, int signedRaw)
    {
        int out = clampInt(signedRaw, -kMaxSignedOutRaw, kMaxSignedOutRaw);
        uint16_t encoded = encodeSignedTorque(out);
        data2Lo = static_cast<uint8_t>((encoded >> 8) & 0x0F);
        data3 = static_cast<uint8_t>(encoded & 0xFF);
    }

    void applyToFrame(uint8_t &data2Lo, uint8_t &data3, int signedRaw)
    {
        setSignedTorqueInFrame(data2Lo, data3, signedRaw);
    }

    // Legacy compatibility shim: despite the old "Delta" name, v4 writes an
    // absolute signed torque target through the hard-capped ±180 raw path.
    void applyDeltaToFrame(uint8_t &data2Lo, uint8_t &data3, int signedRaw)
    {
        applyToFrame(data2Lo, data3, signedRaw);
    }

    void cancel(const char *reason)
    {
        if (mode_ == HumanReplayMode::COOLDOWN &&
            (reasonEquals(blockedReason_, "abort") || reasonEquals(blockedReason_, "txFail")))
            return;
        mode_ = HumanReplayMode::IDLE;
        injecting = false;
        nagActive_ = false;
        blockedReason_ = reason ? reason : "";
        if (reason && reason[0]) gateBlocks_++;
    }

    void enterBurstOn(unsigned long nowMs)
    {
        mode_ = HumanReplayMode::BURST_ON;
        injecting = true;
        phaseStartMs_ = nowMs;
        torqueIndex_ = 0;
        blockedReason_ = "";
        burstSessions_++;
        burstOnEntries_++;
    }

    void enterBurstOff(unsigned long nowMs)
    {
        mode_ = HumanReplayMode::BURST_OFF;
        injecting = false;
        phaseStartMs_ = nowMs;
        burstOffEntries_++;
        burstCyclesCompleted_++;
    }

    void enterCooldown(unsigned long nowMs, const char *reason, unsigned long durationMs)
    {
        mode_ = HumanReplayMode::COOLDOWN;
        injecting = false;
        cooldownStartMs_ = nowMs;
        cooldownDurationMs_ = durationMs;
        blockedReason_ = reason ? reason : "cooldown";
    }

    void enterAbortCooldown(unsigned long nowMs)
    {
        abortBlocks_++;
        enterCooldown(nowMs, "abort", kAbortCooldownMs);
    }

    void failReplayTx(unsigned long nowMs)
    {
        txFailures_++;
        enterCooldown(nowMs, "txFail", kTxFailCooldownMs);
    }

    int peekReplayDelta(unsigned long nowMs) const
    {
        if (!shouldEcho(nowMs)) return 0;
        return TSL6P_TORQUE_RAW[torqueIndex_ % kTorqueSequenceLen];
    }

    void commitReplayDelta(int signedRaw)
    {
        if (mode_ != HumanReplayMode::BURST_ON) return;
        lastTorqueRaw_ = clampInt(signedRaw, -kMaxSignedOutRaw, kMaxSignedOutRaw);
        lastOutDeltaRaw_ = lastTorqueRaw_;
        torqueIndex_ = static_cast<uint8_t>((torqueIndex_ + 1) % kTorqueSequenceLen);
    }

    int nextReplayDelta(unsigned long nowMs)
    {
        if (!shouldEcho(nowMs)) return 0;
        int target = peekReplayDelta(nowMs);
        commitReplayDelta(target);
        return target;
    }

    // Backward-compatible name used by LegacyHandler's 0x370 echo path.
    int computeHold(unsigned long nowMs) { return nextReplayDelta(nowMs); }

    void recordHosClear(uint8_t hos, HumanReplayMode clearPhase)
    {
        if (clearPhase == HumanReplayMode::BURST_ON)
            hosClearDuringOn_++;
        else if (clearPhase == HumanReplayMode::BURST_OFF)
            hosClearDuringOff_++;
        else if (clearPhase == HumanReplayMode::COOLDOWN)
        {
            hosClearWhileCooldown_++;
            lastHosAfter_ = hos;
            return;
        }
        else if (clearPhase == HumanReplayMode::IDLE)
        {
            hosClearWhileIdle_++;
            lastHosAfter_ = hos;
            return;
        }
        else
            return;
        hosClearEvents_++;
        lastHosAfter_ = hos;
    }

    // Called per 0x399. hos = (0x399 data[5]>>2)&0x0F. active = all opt-in/AP/checkAD gates pass.
    void onNagSample(uint8_t hos, unsigned long nowMs, bool active, uint8_t apState, const char *gateReason)
    {
        lastHandsOnState = hos;
        lastApState_ = apState;

        if (isAbortState(apState))
        {
            nagActive_ = hos > 2;
            if (hos > 2)
            {
                nagSamples_++;
                lastHosBefore_ = hos;
            }
            else
            {
                lastHosAfter_ = hos;
            }
            enterAbortCooldown(nowMs);
            return;
        }

        retireExpiredCooldown(nowMs);

        if (hos <= 2)
        {
            if (txFailCooldownActive(nowMs) || abortCooldownActive(nowMs))
            {
                nagActive_ = false;
                recordHosClear(hos, HumanReplayMode::COOLDOWN);
                return;
            }
            HumanReplayMode clearPhase = classifyHosClearPhase(nowMs);
            nagActive_ = false;
            recordHosClear(hos, clearPhase);
            mode_ = HumanReplayMode::IDLE;
            injecting = false;
            blockedReason_ = "";
            return;
        }

        nagSamples_++;
        lastHosBefore_ = hos;

        if (!active)
        {
            if (mode_ == HumanReplayMode::IDLE && !nagActive_)
            {
                blockedReason_ = gateReason ? gateReason : "toggle";
                return;
            }
            cancel(gateReason ? gateReason : "toggle");
            return;
        }

        nagActive_ = true;

        if (mode_ == HumanReplayMode::COOLDOWN)
        {
            if (cooldownRemainMs(nowMs) > 0)
                return;
            mode_ = HumanReplayMode::IDLE;
            injecting = false;
            blockedReason_ = "";
        }

        if (mode_ == HumanReplayMode::IDLE)
        {
            enterBurstOn(nowMs);
            return;
        }

        advance(nowMs, active);
    }

    void onNagSample(uint8_t hos, unsigned long nowMs, bool active, uint8_t apState)
    {
        onNagSample(hos, nowMs, active, apState, active ? nullptr : "toggle");
    }

    // Legacy/no-AP-state-diagnostics compatibility overload. Callers that have
    // the real 0x399 AP state must use the 4-argument overload so abort states
    // 8/9 are visible to the state machine; do not infer abort from active=false.
    void onNagSample(uint8_t hos, unsigned long nowMs, bool active)
    {
        onNagSample(hos, nowMs, active, 6, active ? nullptr : "toggle");
    }

    DashReactiveDiag diag(unsigned long nowMs) const
    {
        DashReactiveDiag d;
        d.mode = mode_;
        d.injecting = injecting;
        d.lastHandsOnState = lastHandsOnState;
        d.currentAmp = currentAmp(nowMs);
        d.nagSamples = nagSamples_;
        d.reactiveBursts = reactiveBursts();
        d.proactiveWiggles = proactiveWiggles();
        d.echoSent = echoSent();
        d.nextProactiveInMs = nextProactiveInMs(nowMs);
        d.replayAttempts = replayAttempts();
        d.replaySuccesses = replaySuccesses();
        d.replayFailures = replayFailures();
        d.lastProfileId = lastProfileId();
        d.lastProfileDir = lastProfileDir();
        d.lastPeakRaw = lastPeakRaw();
        d.lastBaseRaw = lastBaseRaw_;
        d.lastOutDeltaRaw = lastOutDeltaRaw_;
        d.profileIndex = torqueIndex_;
        d.lastHosBefore = lastHosBefore_;
        d.lastHosAfter = lastHosAfter_;
        d.cooldownRemainMs = cooldownRemainMs(nowMs);
        d.blockedReason = blockedReason_;
        d.burstSessions = burstSessions_;
        d.burstOnEntries = burstOnEntries_;
        d.burstOffEntries = burstOffEntries_;
        d.burstFramesSent = burstFramesSent_;
        d.burstCyclesCompleted = burstCyclesCompleted_;
        d.hosClearEvents = hosClearEvents_;
        d.hosClearDuringOn = hosClearDuringOn_;
        d.hosClearDuringOff = hosClearDuringOff_;
        d.hosClearWhileIdle = hosClearWhileIdle_;
        d.hosClearWhileCooldown = hosClearWhileCooldown_;
        d.abortBlocks = abortBlocks_;
        d.gateBlocks = gateBlocks_;
        d.txFailures = txFailures_;
        d.lastApState = lastApState_;
        d.phaseRemainMs = phaseRemainMs(nowMs);
        d.lastTorqueRaw = lastTorqueRaw_;
        d.lastTorqueNmX100 = lastTorqueNmX100();
        return d;
    }
};
