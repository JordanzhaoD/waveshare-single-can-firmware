#pragma once

// dash_epas_late_echo.h — pure EPAS-faithful 0x370 late echo core.
// Header-only and native-testable: no hardware I/O, no immediate sends.

#include <climits>
#include <cstdint>
#include <cstring>

#include "can_frame_types.h"

class DashEpasCadenceTracker
{
public:
    static constexpr uint32_t kEpasId = 880;
    static constexpr unsigned long kMinPeriodMs = 35;
    static constexpr unsigned long kMaxPeriodMs = 45;
    static constexpr unsigned long kMaxJitterMs = 5;
    static constexpr unsigned long kStaleMs = 100;

    void onRx370(const CanFrame &frame, unsigned long nowMs)
    {
        if (frame.id != kEpasId || frame.dlc < 8)
            return;

        const uint8_t counter = frame.data[6] & 0x0F;
        lastSource_ = frame;
        haveSource_ = true;

        if (!haveLast_)
        {
            haveLast_ = true;
            lastCounter_ = counter;
            lastRxMs_ = nowMs;
            cleanFrames_ = 1;
            reason_ = "";
            return;
        }

        const unsigned long interval = nowMs - lastRxMs_;
        const uint8_t step = static_cast<uint8_t>((counter - lastCounter_) & 0x0F);

        if (interval < kMinPeriodMs || interval > kMaxPeriodMs)
        {
            markUnstable("cadenceUnstable");
        }
        else if (haveStep_ && step != counterStep_)
        {
            markUnstable("counterUnstable");
        }
        else if (!haveStep_ && step == 0)
        {
            markUnstable("counterUnstable");
        }
        else
        {
            if (!haveStep_)
            {
                counterStep_ = step;
                haveStep_ = true;
            }

            if (intervals_ == 0)
                periodMs_ = static_cast<uint16_t>(interval);
            else
                periodMs_ = static_cast<uint16_t>(((static_cast<unsigned long>(periodMs_) * intervals_) + interval) / (intervals_ + 1));
            intervals_++;

            const unsigned long base = periodMs_;
            const unsigned long jitter = (interval > base) ? (interval - base) : (base - interval);
            if (jitter > jitterMs_)
                jitterMs_ = static_cast<uint16_t>(jitter);

            if (jitterMs_ > kMaxJitterMs)
                markUnstable("cadenceUnstable");
            else
            {
                if (cleanFrames_ < 255)
                    cleanFrames_++;
                if (cleanFrames_ >= 8)
                    reason_ = "";
            }
        }

        lastCounter_ = counter;
        lastRxMs_ = nowMs;
    }

    bool stable() const { return cleanFrames_ >= 8 && reason_[0] == '\0'; }

    bool lateEchoEligible(unsigned long nowMs) const
    {
        return stable() && haveSource_ && (nowMs - lastRxMs_) <= kStaleMs;
    }

    uint16_t periodMs() const { return periodMs_; }
    uint16_t jitterMs() const { return jitterMs_; }
    uint8_t counterStep() const { return counterStep_; }
    uint8_t expectedNextCounter() const { return static_cast<uint8_t>((lastCounter_ + counterStep_) & 0x0F); }
    unsigned long predictedNextRxMs() const { return lastRxMs_ + periodMs_; }
    const char *blockedReason() const { return reason_; }
    const CanFrame &lastSource() const { return lastSource_; }
    unsigned long lastRxMs() const { return lastRxMs_; }
    bool haveSource() const { return haveSource_; }

private:
    void markUnstable(const char *reason)
    {
        reason_ = reason;
        cleanFrames_ = 1;
        haveStep_ = false;
        intervals_ = 0;
        periodMs_ = 0;
        jitterMs_ = 0;
    }

    bool haveLast_{false};
    bool haveSource_{false};
    bool haveStep_{false};
    uint8_t cleanFrames_{0};
    uint8_t intervals_{0};
    uint8_t lastCounter_{0};
    uint8_t counterStep_{1};
    uint16_t periodMs_{0};
    uint16_t jitterMs_{0};
    unsigned long lastRxMs_{0};
    const char *reason_{""};
    CanFrame lastSource_{};
};

class DashEpasLateEcho;
#ifdef NATIVE_BUILD
struct DashEpasFaithfulEncoderTestAccess;
#endif

class DashEpasFaithfulEncoder
{
    friend class DashEpasLateEcho;
#ifdef NATIVE_BUILD
    friend struct DashEpasFaithfulEncoderTestAccess;
#endif

    static constexpr uint32_t kEpasId = 880;
    static constexpr int kMaxTorqueRaw = 180;

    static bool build(const CanFrame &source, uint8_t expectedCounter, int targetTorqueRaw, CanFrame &out)
    {
        if (source.id != kEpasId || source.dlc < 8)
            return false;

        if (targetTorqueRaw > kMaxTorqueRaw)
            targetTorqueRaw = kMaxTorqueRaw;
        else if (targetTorqueRaw < -kMaxTorqueRaw)
            targetTorqueRaw = -kMaxTorqueRaw;

        out = {};
        out.id = kEpasId;
        out.dlc = 8;
        out.bus = source.bus;
        out.data[0] = source.data[0];
        out.data[1] = source.data[1];

        const uint16_t encoded = static_cast<uint16_t>((targetTorqueRaw + 0x800) & 0x0FFF);
        out.data[2] = static_cast<uint8_t>((source.data[2] & 0xF0) | ((encoded >> 8) & 0x0F));
        out.data[3] = static_cast<uint8_t>(encoded & 0xFF);

        // Stage-1 Legacy validation: reproduce the hands-on level observed in
        // the successful physical-wheel trace. Keep byte4's lower six source
        // bits, clear an inherited level-2 flag, and assert level 1 (bit 6).
        out.data[4] = static_cast<uint8_t>((source.data[4] & 0x3F) | 0x40);
        out.data[5] = source.data[5];
        out.data[6] = static_cast<uint8_t>((source.data[6] & 0xF0) | (expectedCounter & 0x0F));

        uint16_t sum = 0;
        for (int i = 0; i < 7; ++i)
            sum += out.data[i];
        out.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
        return true;
    }
};

#ifdef NATIVE_BUILD
struct DashEpasFaithfulEncoderTestAccess
{
    static bool build(const CanFrame &source, uint8_t expectedCounter, int targetTorqueRaw, CanFrame &out)
    {
        return DashEpasFaithfulEncoder::build(source, expectedCounter, targetTorqueRaw, out);
    }
};
#endif

enum class LateEchoModeState
{
    IDLE,
    BURST_ON,
    BURST_OFF,
    COOLDOWN
};

struct DashEpasLateEchoTxToken
{
    uint32_t generation{0};
    unsigned long dueAtMs{0};
    bool valid{false};
};

struct DashEpasLateEchoDiag
{
    bool enabled{false};
    LateEchoModeState mode{LateEchoModeState::IDLE};
    bool pendingEcho{false};
    unsigned long pendingSendAtMs{0};
    unsigned long cooldownRemainMs{0};
    unsigned long phaseRemainMs{0};
    const char *blockedReason{""};

    bool cadenceStable{false};
    bool lateEchoEligible{false};
    uint16_t periodMs{0};
    uint16_t jitterMs{0};
    uint8_t counterStep{0};
    uint8_t expectedNextCounter{0};
    unsigned long predictedNextRxMs{0};

    uint32_t scheduledEchoes{0};
    uint32_t sentLateEchoes{0};
    uint32_t lateWindowMissed{0};
    uint32_t droppedLateEchoes{0};
    uint32_t abortBlocks{0};
    uint32_t gateBlocks{0};
    uint32_t txFailures{0};
    int lastRxToTxMs{0};
    int lastTxToNextOemMs{0};
    uint32_t counterCollisions{0};
    uint8_t lastSourceCounter{0};
    uint8_t lastInjectedCounter{0};
    uint8_t lastNextOemCounter{0};
    uint8_t profileIndex{0};
    uint8_t replayAttempts{0};
    uint32_t replaySuccesses{0};
    uint32_t replayFailures{0};
    uint8_t lastHosBefore{0};
    uint8_t lastHosAfter{0};
    int lastTargetTorqueRaw{0};
    bool assertsHandsOnLevel1{false};
    uint8_t lastSourceHandsOnLevel{0};
    uint8_t lastTxHandsOnLevel{0};
    uint8_t lastApState{0};
    uint8_t lastHos{0};
};

class DashEpasLateEcho
{
public:
    static constexpr unsigned long kBurstOnMs = 1000;
    static constexpr unsigned long kBurstOffMs = 1500;
    static constexpr unsigned long kAbortCooldownMs = 3000;
    static constexpr unsigned long kTxFailCooldownMs = 3000;
    static constexpr unsigned long kTxResultTimeoutMs = 100;
    // Send immediately after the observed OEM frame. Scheduling near the next
    // OEM slot made the injected C+1 frame arrive 2-9 ms before the OEM C+1
    // frame on the Legacy vehicle, yielding a diagnostic counter collision.
    static constexpr unsigned long kPostRxDelayMs = 1;
    static constexpr unsigned long kMaxLatenessMs = 2;
    static constexpr unsigned long kMaxDasStaleMs = 500;
    static constexpr uint8_t kHumanProfileLen = 25;
    static constexpr uint8_t kMaxReplayAttempts = 2;
    static constexpr unsigned long kReplayFailureCooldownMs = 5000;

    void resetSessionCounters()
    {
        scheduledEchoes_ = 0;
        sentLateEchoes_ = 0;
        lateWindowMissed_ = 0;
        droppedLateEchoes_ = 0;
        abortBlocks_ = 0;
        gateBlocks_ = 0;
        txFailures_ = 0;
        counterCollisions_ = 0;
        replaySuccesses_ = 0;
        replayFailures_ = 0;
        lastRxToTxMs_ = 0;
        lastTxToNextOemMs_ = 0;
    }

    void setEnabled(bool value)
    {
        enabled_ = value;
        if (!enabled_)
        {
            pendingEcho_ = false;
            builtPending_ = false;
            if (mode_ != LateEchoModeState::COOLDOWN)
            {
                blockedReason_ = "disabled";
                mode_ = LateEchoModeState::IDLE;
            }
        }
    }

    bool enabled() const { return enabled_; }

    void onDasStatus(uint8_t apState, uint8_t hos, unsigned long nowMs, bool gatesActive, const char *gateReason)
    {
        const uint8_t previousHos = lastHos_;
        lastApState_ = apState;
        lastHos_ = hos;
        lastDasStatusMs_ = nowMs;
        haveDasStatus_ = true;
        retireCooldown(nowMs);
        advanceBurst(nowMs);
        expireInFlight(nowMs);

        if (apState == 8 || apState == 9)
        {
            apEligible_ = false;
            abortBlocks_++;
            enterCooldown(nowMs, kAbortCooldownMs, "abort");
            return;
        }

        if (builtPending_)
        {
            blockedReason_ = "inFlight";
            return;
        }

        if (mode_ == LateEchoModeState::COOLDOWN)
        {
            pendingEcho_ = false;
            builtPending_ = false;
            return;
        }

        apEligible_ = isEligibleApState(apState);
        if (!apEligible_)
        {
            cancel("apInactive");
            if (mode_ != LateEchoModeState::BURST_OFF)
                mode_ = LateEchoModeState::IDLE;
            return;
        }

        if (hos <= 2)
        {
            if (previousHos > 2 && replayAttempts_ > 0)
            {
                replaySuccesses_++;
                lastHosBefore_ = previousHos;
                lastHosAfter_ = hos;
            }
            cancel("hosClear");
            mode_ = LateEchoModeState::IDLE;
            replayAttempts_ = 0;
            profileIndex_ = 0;
            return;
        }

        if (!enabled_)
        {
            cancel("disabled");
            return;
        }

        if (!gatesActive)
        {
            gateBlocks_++;
            // Keep DAS/HOS state current while the final TX gate is closed.
            // Do not consume a replay attempt until every gate allows TX.
            cancel(gateBlockReason(gateReason));
            mode_ = LateEchoModeState::IDLE;
            replayAttempts_ = 0;
            profileIndex_ = 0;
            return;
        }

        if (mode_ == LateEchoModeState::IDLE)
        {
            replayAttempts_ = 0;
            lastHosBefore_ = hos;
            enterBurstOn(nowMs);
        }
    }

    void onEpasFrame(const CanFrame &frame, unsigned long nowMs, bool gatesActive)
    {
        retireCooldown(nowMs);
        advanceBurst(nowMs);
        expireInFlight(nowMs);

        if (frame.id != DashEpasCadenceTracker::kEpasId || frame.dlc < 8)
        {
            if (mode_ == LateEchoModeState::COOLDOWN)
            {
                pendingEcho_ = false;
                builtPending_ = false;
            }
            else
                cancel("invalidFrame");
            return;
        }

        if (!checksumValid370(frame))
        {
            if (mode_ == LateEchoModeState::COOLDOWN)
            {
                pendingEcho_ = false;
                builtPending_ = false;
            }
            else
                cancel("checksumInvalid");
            return;
        }

        const uint8_t sourceCounter = static_cast<uint8_t>(frame.data[6] & 0x0F);
        lastSourceCounter_ = sourceCounter;
        lastObservedTorqueRaw_ = decodeSignedTorque(frame);
        if (awaitingNextOem_)
        {
            lastNextOemCounter_ = sourceCounter;
            lastTxToNextOemMs_ = static_cast<int>(nowMs - lastTxAtMs_);
            if (sourceCounter == lastInjectedCounter_)
                counterCollisions_++;
            awaitingNextOem_ = false;
        }

        if (builtPending_)
        {
            cadence_.onRx370(frame, nowMs);
            blockedReason_ = "inFlight";
            return;
        }

        if (mode_ == LateEchoModeState::COOLDOWN)
        {
            cadence_.onRx370(frame, nowMs);
            pendingEcho_ = false;
            builtPending_ = false;
            return;
        }

        if (!apEligible_)
        {
            cadence_.onRx370(frame, nowMs);
            cancel("apInactive");
            if (mode_ != LateEchoModeState::BURST_OFF)
                mode_ = LateEchoModeState::IDLE;
            return;
        }

        if (!enabled_ || !gatesActive)
        {
            cadence_.onRx370(frame, nowMs);
            if (!gatesActive)
                gateBlocks_++;
            cancel(!enabled_ ? "disabled" : "gate");
            if (!gatesActive)
            {
                mode_ = LateEchoModeState::IDLE;
                replayAttempts_ = 0;
                profileIndex_ = 0;
            }
            return;
        }

        if (mode_ != LateEchoModeState::BURST_ON)
        {
            cadence_.onRx370(frame, nowMs);
            if (pendingEcho_)
                cancel(mode_ == LateEchoModeState::COOLDOWN ? blockedReason_ : "inactive");
            return;
        }

        const bool hadPending = pendingEcho_;
        if (pendingEcho_)
        {
            if (timeBefore(nowMs, pendingSendAtMs_))
            {
                pendingEcho_ = false;
                builtPending_ = false;
                lateWindowMissed_++;
                blockedReason_ = "lateWindowMissed";
                cadence_.onRx370(frame, nowMs);
                return;
            }
            if (inDueWindow(nowMs))
            {
                pendingEcho_ = false;
                builtPending_ = false;
                lateWindowMissed_++;
                blockedReason_ = "lateWindowMissed";
            }
            else
            {
                pendingEcho_ = false;
                builtPending_ = false;
                droppedLateEchoes_++;
                blockedReason_ = "lateWindowMissed";
            }
        }

        cadence_.onRx370(frame, nowMs);

        if (!cadence_.lateEchoEligible(nowMs))
        {
            pendingEcho_ = false;
            builtPending_ = false;
            blockedReason_ = cadence_.blockedReason();
            if (hadPending && (!blockedReason_ || blockedReason_[0] == '\0'))
                blockedReason_ = "cadenceUnstable";
            return;
        }

        pendingEcho_ = true;
        builtPending_ = false;
        pendingGeneration_++;
        scheduledEchoes_++;
        pendingSendAtMs_ = nowMs + kPostRxDelayMs;
        pendingTorqueRaw_ = targetTorqueRaw(pendingSendAtMs_);
        lastTargetTorqueRaw_ = pendingTorqueRaw_;
        blockedReason_ = "";
    }

    bool due(unsigned long nowMs) const
    {
        return enabled_ && apEligible_ && mode_ == LateEchoModeState::BURST_ON && elapsedSince(phaseStartMs_, nowMs) < kBurstOnMs && pendingEcho_ && !builtPending_ && inDueWindow(nowMs);
    }

    bool buildDueFrame(unsigned long nowMs, CanFrame &out, bool gatesActive, uint8_t currentApState, uint8_t currentHos, const char *gateReason, DashEpasLateEchoTxToken &token)
    {
        token = {};
        lastApState_ = currentApState;
        lastHos_ = currentHos;
        retireCooldown(nowMs);
        advanceBurst(nowMs);
        expireInFlight(nowMs);
        if (builtPending_)
        {
            blockedReason_ = "inFlight";
            return false;
        }
        if (mode_ == LateEchoModeState::COOLDOWN)
        {
            pendingEcho_ = false;
            builtPending_ = false;
            return false;
        }
        if (currentApState == 8 || currentApState == 9)
        {
            apEligible_ = false;
            abortBlocks_++;
            enterCooldown(nowMs, kAbortCooldownMs, "abort");
            return false;
        }
        apEligible_ = isEligibleApState(currentApState);
        if (!apEligible_)
        {
            cancel("apInactive");
            return false;
        }
        if (!gatesActive)
        {
            gateBlocks_++;
            cancel(gateBlockReason(gateReason));
            mode_ = LateEchoModeState::IDLE;
            replayAttempts_ = 0;
            profileIndex_ = 0;
            return false;
        }
        if (currentHos <= 2)
        {
            cancel("hosClear");
            return false;
        }
        if (!enabled_)
        {
            cancel("disabled");
            return false;
        }
        if (!apEligible_)
        {
            cancel("apInactive");
            return false;
        }
        if (mode_ != LateEchoModeState::BURST_ON)
        {
            if (pendingEcho_)
                cancel("burstOff");
            else
                blockedReason_ = "burstOff";
            return false;
        }
        if (!dasFresh(nowMs))
        {
            gateBlocks_++;
            cancel("dasStale");
            return false;
        }
        if (!due(nowMs))
        {
            if (pendingEcho_ && !timeBefore(nowMs, pendingSendAtMs_) && !inDueWindow(nowMs))
            {
                pendingEcho_ = false;
                builtPending_ = false;
                droppedLateEchoes_++;
                blockedReason_ = "lateWindowMissed";
            }
            return false;
        }
        if (!cadence_.lateEchoEligible(nowMs))
        {
            cancel(cadence_.blockedReason()[0] ? cadence_.blockedReason() : "cadenceUnstable");
            return false;
        }
        if (!DashEpasFaithfulEncoder::build(cadence_.lastSource(), cadence_.expectedNextCounter(), pendingTorqueRaw_, out))
            return false;
        assertsHandsOnLevel1_ = true;
        lastSourceHandsOnLevel_ = static_cast<uint8_t>((cadence_.lastSource().data[4] >> 6) & 0x03);
        lastTxHandsOnLevel_ = static_cast<uint8_t>((out.data[4] >> 6) & 0x03);
        builtPending_ = true;
        builtAtMs_ = nowMs;
        token.generation = pendingGeneration_;
        token.dueAtMs = pendingSendAtMs_;
        token.valid = true;
        return true;
    }

    void notifyTxResult(const DashEpasLateEchoTxToken &token, bool ok, unsigned long nowMs)
    {
        if (!token.valid || token.generation != pendingGeneration_ || token.dueAtMs != pendingSendAtMs_ || !builtPending_)
            return;

        if (ok)
        {
            if (pendingEcho_)
            {
                sentLateEchoes_++;
                lastRxToTxMs_ = static_cast<int>(nowMs - cadence_.lastRxMs());
                lastInjectedCounter_ = cadence_.expectedNextCounter();
                lastTxAtMs_ = nowMs;
                awaitingNextOem_ = true;
                if (profileIndex_ < kHumanProfileLen)
                    profileIndex_++;
            }
            pendingEcho_ = false;
            builtPending_ = false;
            blockedReason_ = "";
            if (profileIndex_ >= kHumanProfileLen)
                enterBurstOff(nowMs);
            return;
        }

        txFailures_++;
        enterCooldown(nowMs, kTxFailCooldownMs, "txFail");
    }

    DashEpasLateEchoDiag diag(unsigned long nowMs) const
    {
        DashEpasLateEchoDiag d;
        d.enabled = enabled_;
        d.mode = mode_;
        d.pendingEcho = pendingEcho_;
        d.pendingSendAtMs = pendingSendAtMs_;
        d.cooldownRemainMs = cooldownRemain(nowMs);
        d.phaseRemainMs = phaseRemain(nowMs);
        d.blockedReason = blockedReason_;
        d.cadenceStable = cadence_.stable();
        d.lateEchoEligible = cadence_.lateEchoEligible(nowMs);
        d.periodMs = cadence_.periodMs();
        d.jitterMs = cadence_.jitterMs();
        d.counterStep = cadence_.counterStep();
        d.expectedNextCounter = cadence_.expectedNextCounter();
        d.predictedNextRxMs = cadence_.predictedNextRxMs();
        d.scheduledEchoes = scheduledEchoes_;
        d.sentLateEchoes = sentLateEchoes_;
        d.lateWindowMissed = lateWindowMissed_;
        d.droppedLateEchoes = droppedLateEchoes_;
        d.abortBlocks = abortBlocks_;
        d.gateBlocks = gateBlocks_;
        d.txFailures = txFailures_;
        d.lastRxToTxMs = lastRxToTxMs_;
        d.lastTxToNextOemMs = lastTxToNextOemMs_;
        d.counterCollisions = counterCollisions_;
        d.lastSourceCounter = lastSourceCounter_;
        d.lastInjectedCounter = lastInjectedCounter_;
        d.lastNextOemCounter = lastNextOemCounter_;
        d.profileIndex = profileIndex_;
        d.replayAttempts = replayAttempts_;
        d.replaySuccesses = replaySuccesses_;
        d.replayFailures = replayFailures_;
        d.lastHosBefore = lastHosBefore_;
        d.lastHosAfter = lastHosAfter_;
        d.lastTargetTorqueRaw = lastTargetTorqueRaw_;
        d.assertsHandsOnLevel1 = assertsHandsOnLevel1_;
        d.lastSourceHandsOnLevel = lastSourceHandsOnLevel_;
        d.lastTxHandsOnLevel = lastTxHandsOnLevel_;
        d.lastApState = lastApState_;
        d.lastHos = lastHos_;
        return d;
    }

private:
    static int decodeSignedTorque(const CanFrame &frame)
    {
        const int encoded = static_cast<int>(((frame.data[2] & 0x0F) << 8) | frame.data[3]);
        return encoded - 0x800;
    }
    static bool isEligibleApState(uint8_t apState)
    {
        return apState == 3 || apState == 4 || apState == 5 || apState == 6;
    }

    static const char *gateBlockReason(const char *reason)
    {
        if (!reason)
            return "gate";
        if (std::strcmp(reason, "finalGateLost") == 0)
            return "finalGateLost";
        if (std::strcmp(reason, "gate") == 0)
            return "gate";
        if (std::strcmp(reason, "toggle") == 0)
            return "toggle";
        if (std::strcmp(reason, "checkAD") == 0)
            return "checkAD";
        return "gate";
    }

    static bool checksumValid370(const CanFrame &frame)
    {
        uint16_t sum = 0;
        for (int i = 0; i < 7; ++i)
            sum += frame.data[i];
        return static_cast<uint8_t>((sum + 0x73) & 0xFF) == frame.data[7];
    }

    bool dasFresh(unsigned long nowMs) const
    {
        return haveDasStatus_ && elapsedSince(lastDasStatusMs_, nowMs) <= kMaxDasStaleMs;
    }

    void expireInFlight(unsigned long nowMs)
    {
        if (builtPending_ && elapsedSince(builtAtMs_, nowMs) > kTxResultTimeoutMs)
        {
            txFailures_++;
            enterCooldown(nowMs, kTxFailCooldownMs, "txFail");
        }
    }

    int targetTorqueRaw(unsigned long /*nowMs*/) const
    {
        // 25 x 40 ms points reconstructed from the Legacy vehicle's successful
        // manual-dismiss captures. The serial capture logged every other source
        // counter, so intermediate points are restored here instead of replaying
        // the 10-13 visible samples at twice the real speed.
        static constexpr int16_t magnitudeProfile[kHumanProfileLen] = {
            50, 65, 80, 95, 110, 125, 140, 150, 158, 165,
            170, 174, 176, 176, 174, 170, 165, 158, 150, 140,
            128, 115, 100, 82, 65};
        const uint8_t index = profileIndex_ < kHumanProfileLen ? profileIndex_ : kHumanProfileLen - 1;
        const int magnitude = magnitudeProfile[index];
        return burstDirection_ > 0 ? magnitude : -magnitude;
    }

    void cancel(const char *reason)
    {
        pendingEcho_ = false;
        builtPending_ = false;
        blockedReason_ = reason ? reason : "";
    }

    void enterBurstOn(unsigned long nowMs)
    {
        mode_ = LateEchoModeState::BURST_ON;
        phaseStartMs_ = nowMs;
        profileIndex_ = 0;
        replayAttempts_++;
        if (replayAttempts_ == 1 && (lastObservedTorqueRaw_ >= 20 || lastObservedTorqueRaw_ <= -20))
            burstDirection_ = lastObservedTorqueRaw_ > 0 ? 1 : -1;
        else
            burstDirection_ = nextBurstDirection_;
        nextBurstDirection_ = static_cast<int8_t>(-burstDirection_);
        blockedReason_ = "";
    }

    void enterBurstOff(unsigned long nowMs)
    {
        pendingEcho_ = false;
        builtPending_ = false;
        mode_ = LateEchoModeState::BURST_OFF;
        phaseStartMs_ = nowMs;
    }

    void enterCooldown(unsigned long nowMs, unsigned long durationMs, const char *reason)
    {
        pendingEcho_ = false;
        builtPending_ = false;
        mode_ = LateEchoModeState::COOLDOWN;
        cooldownStartMs_ = nowMs;
        cooldownDurationMs_ = durationMs;
        blockedReason_ = reason ? reason : "";
    }

    void retireCooldown(unsigned long nowMs)
    {
        if (mode_ == LateEchoModeState::COOLDOWN && elapsedSince(cooldownStartMs_, nowMs) >= cooldownDurationMs_)
        {
            mode_ = LateEchoModeState::IDLE;
            cooldownDurationMs_ = 0;
            if (blockedReason_ && (std::strcmp(blockedReason_, "abort") == 0 || std::strcmp(blockedReason_, "txFail") == 0))
                blockedReason_ = "";
        }
    }

    static unsigned long elapsedSince(unsigned long startMs, unsigned long nowMs)
    {
        return nowMs - startMs;
    }

    static bool timeBefore(unsigned long candidateMs, unsigned long referenceMs)
    {
        return candidateMs != referenceMs && (referenceMs - candidateMs) < (ULONG_MAX / 2UL + 1UL);
    }

    bool inDueWindow(unsigned long nowMs) const
    {
        return elapsedSince(pendingSendAtMs_, nowMs) <= kMaxLatenessMs;
    }

    unsigned long cooldownRemain(unsigned long nowMs) const
    {
        if (mode_ != LateEchoModeState::COOLDOWN)
            return 0;
        const unsigned long elapsed = elapsedSince(cooldownStartMs_, nowMs);
        return elapsed < cooldownDurationMs_ ? (cooldownDurationMs_ - elapsed) : 0;
    }

    void advanceBurst(unsigned long nowMs)
    {
        if (mode_ == LateEchoModeState::BURST_ON && elapsedSince(phaseStartMs_, nowMs) >= kBurstOnMs)
            enterBurstOff(phaseStartMs_ + kBurstOnMs);
        if (mode_ == LateEchoModeState::BURST_OFF && elapsedSince(phaseStartMs_, nowMs) >= kBurstOffMs)
        {
            if (lastHos_ > 2 && replayAttempts_ < kMaxReplayAttempts)
                enterBurstOn(phaseStartMs_ + kBurstOffMs);
            else if (lastHos_ > 2)
            {
                replayFailures_++;
                lastHosAfter_ = lastHos_;
                enterCooldown(nowMs, kReplayFailureCooldownMs, "replayFailed");
            }
            else
                mode_ = LateEchoModeState::IDLE;
        }
    }

    unsigned long phaseRemain(unsigned long nowMs) const
    {
        if (mode_ == LateEchoModeState::BURST_ON)
        {
            const unsigned long elapsed = nowMs - phaseStartMs_;
            return elapsed < kBurstOnMs ? (kBurstOnMs - elapsed) : 0;
        }
        if (mode_ == LateEchoModeState::BURST_OFF)
        {
            const unsigned long elapsed = nowMs - phaseStartMs_;
            return elapsed < kBurstOffMs ? (kBurstOffMs - elapsed) : 0;
        }
        return 0;
    }

    bool enabled_{false};
    LateEchoModeState mode_{LateEchoModeState::IDLE};
    DashEpasCadenceTracker cadence_{};
    bool pendingEcho_{false};
    bool builtPending_{false};
    bool apEligible_{false};
    bool haveDasStatus_{false};
    int pendingTorqueRaw_{0};
    int8_t burstDirection_{1};
    int8_t nextBurstDirection_{1};
    unsigned long pendingSendAtMs_{0};
    unsigned long builtAtMs_{0};
    unsigned long phaseStartMs_{0};
    unsigned long lastDasStatusMs_{0};
    unsigned long cooldownStartMs_{0};
    unsigned long cooldownDurationMs_{0};
    const char *blockedReason_{""};
    uint32_t pendingGeneration_{0};
    uint32_t scheduledEchoes_{0};
    uint32_t sentLateEchoes_{0};
    uint32_t lateWindowMissed_{0};
    uint32_t droppedLateEchoes_{0};
    uint32_t abortBlocks_{0};
    uint32_t gateBlocks_{0};
    uint32_t txFailures_{0};
    int lastRxToTxMs_{0};
    int lastTxToNextOemMs_{0};
    uint32_t counterCollisions_{0};
    uint8_t lastSourceCounter_{0};
    uint8_t lastInjectedCounter_{0};
    uint8_t lastNextOemCounter_{0};
    uint8_t profileIndex_{0};
    uint8_t replayAttempts_{0};
    uint32_t replaySuccesses_{0};
    uint32_t replayFailures_{0};
    uint8_t lastHosBefore_{0};
    uint8_t lastHosAfter_{0};
    int lastTargetTorqueRaw_{0};
    int lastObservedTorqueRaw_{0};
    bool awaitingNextOem_{false};
    unsigned long lastTxAtMs_{0};
    bool assertsHandsOnLevel1_{false};
    uint8_t lastSourceHandsOnLevel_{0};
    uint8_t lastTxHandsOnLevel_{0};
    uint8_t lastApState_{0};
    uint8_t lastHos_{0};
};
