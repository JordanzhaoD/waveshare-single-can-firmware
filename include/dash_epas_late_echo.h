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

struct DashEpasFaithfulEncoder
{
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

        // EPAS-faithful: preserve byte4 exactly. Do not forge hands-on with 0x40.
        out.data[4] = source.data[4];
        out.data[5] = source.data[5];
        out.data[6] = static_cast<uint8_t>((source.data[6] & 0xF0) | (expectedCounter & 0x0F));

        uint16_t sum = 0;
        for (int i = 0; i < 7; ++i)
            sum += out.data[i];
        out.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
        return true;
    }
};

enum class LateEchoModeState
{
    IDLE,
    BURST_ON,
    BURST_OFF,
    COOLDOWN
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
    uint16_t periodMs{0};
    uint16_t jitterMs{0};
    uint8_t counterStep{0};
    uint8_t expectedNextCounter{0};
    unsigned long predictedNextRxMs{0};

    uint32_t sentLateEchoes{0};
    uint32_t lateWindowMissed{0};
    uint32_t droppedLateEchoes{0};
    uint32_t abortBlocks{0};
    uint32_t gateBlocks{0};
    uint32_t txFailures{0};
    int lastRxToTxMs{0};
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
    static constexpr unsigned long kLateEchoLeadMs = 3;
    static constexpr unsigned long kMaxLatenessMs = 2;

    void setEnabled(bool value)
    {
        enabled_ = value;
        if (!enabled_)
            cancel("disabled");
    }

    bool enabled() const { return enabled_; }

    void onDasStatus(uint8_t apState, uint8_t hos, unsigned long nowMs, bool gatesActive, const char *gateReason)
    {
        lastApState_ = apState;
        lastHos_ = hos;
        retireCooldown(nowMs);
        advanceBurst(nowMs);

        if (apState == 8 || apState == 9)
        {
            abortBlocks_++;
            enterCooldown(nowMs, kAbortCooldownMs, "abort");
            return;
        }

        if (mode_ == LateEchoModeState::COOLDOWN)
        {
            pendingEcho_ = false;
            builtPending_ = false;
            return;
        }

        if (hos <= 2)
        {
            cancel("hosClear");
            mode_ = LateEchoModeState::IDLE;
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
            cancel(gateReason ? gateReason : "gate");
            mode_ = LateEchoModeState::IDLE;
            return;
        }

        if (mode_ == LateEchoModeState::IDLE)
            enterBurstOn(nowMs);
    }

    void onEpasFrame(const CanFrame &frame, unsigned long nowMs, bool gatesActive)
    {
        retireCooldown(nowMs);
        advanceBurst(nowMs);

        if (!enabled_ || !gatesActive)
        {
            cadence_.onRx370(frame, nowMs);
            cancel(!enabled_ ? "disabled" : "gate");
            return;
        }

        if (mode_ != LateEchoModeState::BURST_ON)
        {
            cadence_.onRx370(frame, nowMs);
            if (pendingEcho_)
                cancel(mode_ == LateEchoModeState::COOLDOWN ? blockedReason_ : "inactive");
            return;
        }

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
            if (!inDueWindow(nowMs))
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
            blockedReason_ = cadence_.blockedReason();
            return;
        }

        pendingEcho_ = true;
        builtPending_ = false;
        pendingSendAtMs_ = cadence_.predictedNextRxMs() - kLateEchoLeadMs;
        blockedReason_ = "";
    }

    bool due(unsigned long nowMs) const
    {
        return pendingEcho_ && !builtPending_ && inDueWindow(nowMs);
    }

    bool buildDueFrame(unsigned long nowMs, CanFrame &out)
    {
        if (!due(nowMs))
            return false;
        if (!DashEpasFaithfulEncoder::build(cadence_.lastSource(), cadence_.expectedNextCounter(), DashEpasFaithfulEncoder::kMaxTorqueRaw, out))
            return false;
        builtPending_ = true;
        return true;
    }

    void notifyTxResult(bool ok, unsigned long nowMs)
    {
        if (ok)
        {
            if (pendingEcho_ && builtPending_)
            {
                sentLateEchoes_++;
                lastRxToTxMs_ = static_cast<int>(nowMs - cadence_.lastRxMs());
            }
            pendingEcho_ = false;
            builtPending_ = false;
            blockedReason_ = "";
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
        d.periodMs = cadence_.periodMs();
        d.jitterMs = cadence_.jitterMs();
        d.counterStep = cadence_.counterStep();
        d.expectedNextCounter = cadence_.expectedNextCounter();
        d.predictedNextRxMs = cadence_.predictedNextRxMs();
        d.sentLateEchoes = sentLateEchoes_;
        d.lateWindowMissed = lateWindowMissed_;
        d.droppedLateEchoes = droppedLateEchoes_;
        d.abortBlocks = abortBlocks_;
        d.gateBlocks = gateBlocks_;
        d.txFailures = txFailures_;
        d.lastRxToTxMs = lastRxToTxMs_;
        d.lastApState = lastApState_;
        d.lastHos = lastHos_;
        return d;
    }

private:
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
            enterBurstOn(phaseStartMs_ + kBurstOffMs);
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
    unsigned long pendingSendAtMs_{0};
    unsigned long phaseStartMs_{0};
    unsigned long cooldownStartMs_{0};
    unsigned long cooldownDurationMs_{0};
    const char *blockedReason_{""};
    uint32_t sentLateEchoes_{0};
    uint32_t lateWindowMissed_{0};
    uint32_t droppedLateEchoes_{0};
    uint32_t abortBlocks_{0};
    uint32_t gateBlocks_{0};
    uint32_t txFailures_{0};
    int lastRxToTxMs_{0};
    uint8_t lastApState_{0};
    uint8_t lastHos_{0};
};
