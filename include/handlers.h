#pragma once

#include <memory>
#include <algorithm>
#include <cstring>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "shared_types.h"
#include "log_buffer.h"
#include "dash_hw3_speed.h"
#include "dash_legacy_speed.h"
#include "dash_abort_guard.h"
#include "dash_reactive_nag.h"
#include "dash_epas_late_echo.h"
#include "dash_fsd_diag.h"

#ifndef DASH_FSD_252_COMPAT
#define DASH_FSD_252_COMPAT 0
#endif

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif
#endif

inline LogRingBuffer logRing;

static inline bool framePayloadChanged(const CanFrame &original, const CanFrame &modified)
{
    if (original.id != modified.id || original.dlc != modified.dlc)
        return true;

    const uint8_t dlc = (original.dlc <= 8) ? original.dlc : 8;
    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (original.data[i] != modified.data[i])
            return true;
    }
    return false;
}

struct LegacySpeedRuntimeDiag
{
    LegacySmartOffsetResult result{};
    bool gpsSpeedSeen = false;
    bool gpsSpeedFresh = false;
    uint32_t gpsSpeedPeriodMs = 0;
    uint32_t gpsSpeedLastMs = 0;
    uint32_t gpsSpeedCount = 0;
    uint8_t gpsUserOffsetRaw = 0;
    int gpsUserOffsetKph = -30;
    uint8_t gpsMppLimitRaw = 0;
    uint16_t gpsMppLimitKph = 0;
    uint8_t lastSentOffsetRaw = 0;
    uint8_t lastSentOffsetKph = 0;
    uint32_t txOk = 0;
    uint32_t txFail = 0;
    const char *blockedReason = "none";
};

struct CarManagerBase
{
    Shared<int> speedProfile{1};
    Shared<bool> speedProfileAuto{true};
    Shared<bool> ADEnabled{false};
    Shared<bool> APActive{false};
    // Default Parked=true so the AP Injection Gate opens immediately on
    // module boot when the DI is asleep (e.g. car locked with Sentry on,
    // CAN ID 280 not broadcast). The first DI_systemStatus frame with a
    // driving gear (R/N/D) flips this to false; if 280 never arrives,
    // the car is asleep / parked and the gate stays open by design.
    Shared<bool> Parked{true};
    Shared<bool> Summoning{false};
    Shared<int> gatewayAutopilot{-1};
    Shared<bool> enablePrint{true};
    Shared<uint32_t> frameCount{0};
    Shared<uint32_t> framesSent{0};
    Shared<int> speedOffset{0};
    // --- FSD activation state (from tesla-fsd-controller verified logic) ---
    Shared<bool> fsdTriggered{false};
    Shared<bool> removeVisionSpeedLimit{true};
    LegacyFsdDiag legacyFsdDiag{};
    Shared<int> legacyOffset{0};
    Shared<bool> overrideSpeedLimit{false};
    LegacySmartOffsetConfig legacySmartOffsetConfig{};
    LegacySmartOffsetEngine legacySmartOffsetEngine{};
    LegacySpeedRuntimeDiag legacySpeedDiag{};
    DashAbortGuard abortGuard{};
    Shared<bool> tlsscBypass{false};
    Shared<bool> emergencyVehicleDetection{true};
    Shared<bool> isaChimeSuppress{false};
    Shared<bool> isaOverride{false};    // spec Task 3: HW4 重写限速 5b (0x39B raw=31 NONE)
    Shared<bool> bionicSteering{false}; // Phase 3: bionic steering mode
    Shared<uint8_t> hw4OffsetRaw{0};
    Shared<bool> banShieldEnable{false};
    Shared<uint32_t> banShieldBlocks{0};
    Shared<uint8_t> hwDetected{0};
    Shared<bool> autoModeEnabled{false};
    // Ban Shield per-mux state (CAN task only, non-atomic)
    uint8_t banShieldSnapshot[8][8] = {};
    bool banShieldValid[8] = {};

    bool handleBanShield(CanFrame &frame, CanDriver &driver)
    {
        if (!(bool)banShieldEnable)
            return false;
        if (frame.id != 2047 || frame.dlc < 8)
            return false;

        uint8_t mux = readMuxID(frame);
        if (mux >= 8)
            return false;

        if (!banShieldValid[mux])
        {
            for (int i = 0; i < 8; i++)
                banShieldSnapshot[mux][i] = frame.data[i];
            banShieldValid[mux] = true;
            return false;
        }

        bool changed = false;
        for (int i = 0; i < 8; i++)
        {
            if (frame.data[i] != banShieldSnapshot[mux][i])
            {
                changed = true;
                break;
            }
        }
        if (!changed)
            return false;

        if (checkAD && !checkAD())
            return true;
        CanFrame out = frame;
        for (int i = 0; i < 8; i++)
            out.data[i] = banShieldSnapshot[mux][i];
        banShieldBlocks++;
        driver.send(out);
        return true;
    }

    // Auto hardware detection from GTW_carConfig (CAN 920)
    void updateHwDetectedFrom920(const CanFrame &frame)
    {
        if (frame.id != 920 || frame.dlc < 1)
            return;
        uint8_t das_hw = (frame.data[0] >> 6) & 0x03;
        if (das_hw == 2)
            hwDetected = 1; // HW3
        else if (das_hw == 3)
            hwDetected = 2; // HW4
    }

    unsigned long lastSummonActivityMs = 0;
    // Summon-vs-AP/TACC discrimination state. ACA (DI_autonomyControlActive)
    // alone is set during AP, TACC, and Smart Summon, so it cannot be the
    // sole gate signal. We only treat ACA as "summon active" when we have
    // also observed UI_selfParkRequest go non-zero during the current
    // autonomy episode. ACA falling edge clears sprSeen so the next ACA
    // rising edge (e.g. user engaging TACC after a completed summon) does
    // not falsely keep the gate open.
    bool sprSeen = false;
    bool lastAca = false;

    void (*onFrame)(const CanFrame &) = nullptr;
    void (*onSend)(uint8_t mux, bool ok) = nullptr;
    bool (*checkAD)() = nullptr;
    bool (*checkNag)() = nullptr;
    bool (*checkSummon)() = nullptr;
    bool (*checkIsa)() = nullptr;
    bool (*checkEvd)() = nullptr;
    FsdGateBlockReason (*gateBlockReason)() = nullptr;
    bool (*legacyFsdActivationAllowed)(uint32_t nowMs) = nullptr;
    // When set and returns true, an installed plugin owns the FSD activation
    // frames (1006/1021/2047) and the built-in handler must suppress its own
    // injection to avoid duplicate frames. Defaults to nullptr so the built-in
    // path is always allowed (no behavior change for dual CAN / native tests).
    bool (*pluginOwnsFsdActivation)() = nullptr;

    // Returns false only when a plugin currently owns FSD activation, so the
    // built-in send paths should skip. With no callback (default) the built-in
    // path is always allowed.
    bool builtInFsdInjectionAllowed() const
    {
        if (pluginOwnsFsdActivation && pluginOwnsFsdActivation())
            return false;
        return true;
    }

    bool abortGuardAllowsInjection(DashAbortGuardBlockPath path)
    {
        if (abortGuard.allowsInjection())
            return true;
        abortGuard.recordBlock(path);
        return false;
    }

    bool injectionGateOpen() const
    {
        return (bool)APActive || (bool)Parked || (bool)Summoning;
    }

    bool injectionAllowed() const
    {
        if (checkAD && !checkAD())
            return false;
        return enhancedAutopilotInjectionAllowed(injectionGateOpen());
    }

    FsdGateBlockReason currentGateBlockReason() const
    {
        if (gateBlockReason)
            return gateBlockReason();
        if (checkAD && !checkAD())
            return FsdGateBlockReason::CheckAd;
        if (!enhancedAutopilotInjectionAllowed(injectionGateOpen()))
            return FsdGateBlockReason::CompileGate;
        return FsdGateBlockReason::CheckAd;
    }

    // Recompute Summoning from current sprSeen + lastAca state. Summoning
    // requires both: ACA bit currently set AND we have seen at least one
    // UI_selfParkRequest non-zero command in the current autonomy episode.
    // This excludes plain TACC (ACA=1, no spr) and post-AP ACA tail
    // (ACA blip with no fresh spr) from latching the gate.
    void recomputeSummoning()
    {
        Summoning = lastAca && sprSeen;
    }

    // Update summon state from UI_driverAssistControl (CAN ID 1016).
    // Tesla DBC: UI_selfParkRequest at byte 3 bits 4-7 (4=PRIME, 5=PAUSE,
    // 7/8=AUTO_SUMMON_FWD/REV, 11=SMART_SUMMON, 0=NONE). Records that a
    // summon command has been issued during the current autonomy episode.
    void updateSummonFrom1016(const CanFrame &frame)
    {
        if (frame.dlc < 4)
            return;
        uint8_t spr = static_cast<uint8_t>((frame.data[3] >> 4) & 0x0F);
        if (spr != 0)
            sprSeen = true;
        recomputeSummoning();
    }

    // Update summon state from DI_systemStatus (CAN ID 280).
    // Tesla DBC: DI_autonomyControlActive at bit 50 (byte 6 bit 2). Held
    // high while the DI is being driven by AP, TACC, Smart Summon, etc.
    // ACA falling edge ends the autonomy episode and clears sprSeen so a
    // subsequent TACC engagement (ACA=1 again) does not re-latch the gate.
    void updateSummonFromDISystemStatus(const CanFrame &frame)
    {
        if (frame.dlc < 7)
            return;
        bool aca = (frame.data[6] & 0x04) != 0;
        if (lastAca && !aca)
            sprSeen = false;
        lastAca = aca;
        recomputeSummoning();
    }

    // Force Summoning off and reset sprSeen when the vehicle is observed
    // in Park with no active autonomy episode, so a manual P->D shift
    // afterwards correctly waits for AP. During Smart Summon startup the
    // DI can report ACA=1 while gear is still P; keep sprSeen latched so
    // it survives the pending shift out of Park.
    void clearSummonOnPark()
    {
        Summoning = false;
        sprSeen = false;
#ifndef NATIVE_BUILD
        lastSummonActivityMs = 0;
#endif
    }

    void clearSummonOnParkIfAcaInactive(uint8_t gear)
    {
        if (gear == 1 && !lastAca)
            clearSummonOnPark();
    }

    // Process DI_systemStatus (CAN 280) — gear + summon update.
    // Returns true if the frame was handled.
    bool handleDISystemStatus(const CanFrame &frame)
    {
        if (frame.id != 280 || frame.dlc < 3)
            return false;
        uint8_t diGear = readDIGear(frame);
        Parked = isVehicleParked(diGear);
        updateSummonFromDISystemStatus(frame);
        clearSummonOnParkIfAcaInactive(diGear);
        return true;
    }

    // Process DI_vehicleStatus (CAN 390) — gear update.
    // Returns true if the frame was handled.
    bool handleDIVehicleStatus(const CanFrame &frame)
    {
        if (frame.id != 390 || frame.dlc < 8)
            return false;
        uint8_t difGear = readVehicleGear(frame);
        Parked = isVehicleParked(difGear);
        clearSummonOnParkIfAcaInactive(difGear);
        return true;
    }

    bool shouldInjectSpeedProfile() const
    {
#if defined(ESP32_DASHBOARD)
        return !speedProfileAuto;
#else
        return true;
#endif
    }

    virtual void handleMessage(CanFrame &frame, CanDriver &driver) = 0;
    virtual void tick(uint32_t nowMs, CanDriver &driver)
    {
        (void)nowMs;
        (void)driver;
    }
    virtual const uint32_t *filterIds() const = 0;
    virtual uint8_t filterIdCount() const = 0;
    virtual bool bionicDisabled() const { return false; }
    virtual void resetBionic(uint32_t seed) { (void)seed; }
    virtual void setNagMode(uint8_t mode) { (void)mode; }
    virtual DashReactiveDiag reactiveDiag() const { return {}; }
    // Instrumentation counter persistence (NVS round-trip) — survive power-off.
    virtual void resetReactiveCounters() {}
    virtual void setReactiveCounters(uint32_t /*ns*/, uint32_t /*rb*/, uint32_t /*pw*/,
                                     uint32_t /*es*/) {}
    virtual void bumpReactiveCounters() {}
    virtual ~CarManagerBase() = default;
};


struct LegacyHandler : public CarManagerBase
{
    enum class NagMode : uint8_t
    {
        Off = 0,
        LegacyTsl6p = 1,
        EpasLateEcho = 2
    };

    DashReactiveNagBurst nag; // reactive NAG-suppression burst state machine
    DashEpasLateEcho lateNag;
    NagMode nagMode{NagMode::Off};
    uint8_t lastNagApState{0};
    uint8_t lastNagHos{0};
    const char *lastNagGateReason{"toggle"};
    bool lastNagGatesActive{false};

    bool bionicDisabled() const override { return false; } // reactive has no auto-disable
    bool lateEchoSelected() const { return nagMode == NagMode::EpasLateEcho; }
    bool legacyTsl6pSelected() const { return nagMode == NagMode::LegacyTsl6p; }
    bool isPrimaryDasFrame(const CanFrame &frame) const { return frame.bus != CAN_BUS_PARTY; }

    void refreshLateNagEnabled()
    {
        lateNag.setEnabled(lateEchoSelected() && (bool)bionicSteering);
    }

    void setNagMode(uint8_t mode) override
    {
        if (mode == static_cast<uint8_t>(NagMode::LegacyTsl6p))
            nagMode = NagMode::LegacyTsl6p;
        else if (mode == static_cast<uint8_t>(NagMode::EpasLateEcho))
            nagMode = NagMode::EpasLateEcho;
        else
            nagMode = NagMode::Off;
        refreshLateNagEnabled();
    }

    void setNagModeForTest(const char *mode)
    {
        if (mode && std::strcmp(mode, "legacy_tsl6p") == 0)
            setNagMode(static_cast<uint8_t>(NagMode::LegacyTsl6p));
        else if (mode && std::strcmp(mode, "late_echo") == 0)
            setNagMode(static_cast<uint8_t>(NagMode::EpasLateEcho));
        else
            setNagMode(static_cast<uint8_t>(NagMode::Off));
    }

    void resetBionic(uint32_t seed) override
    {
        nag.reset();
        nag.init(seed ? seed : 0xDEADBEEF);
        lateNag = DashEpasLateEcho{};
        refreshLateNagEnabled();
    }

    DashEpasLateEchoDiag lateEchoDiag(uint32_t nowMs) const { return lateNag.diag(nowMs); }

    DashReactiveDiag reactiveDiag() const override
    {
        uint32_t nowMs = dashDiagNowMs();
        DashReactiveDiag d = nag.diag(nowMs);
        d.enabled = (bool)bionicSteering && nagMode != NagMode::Off;
        if (lateEchoSelected())
        {
            DashEpasLateEchoDiag late = lateNag.diag(nowMs);
            d.lateEchoMode = true;
            d.mode = HumanReplayMode::IDLE;
            d.injecting = late.mode == LateEchoModeState::BURST_ON;
            d.currentAmp = 0;
            d.blockedReason = late.blockedReason;
            d.cooldownRemainMs = late.cooldownRemainMs;
            d.phaseRemainMs = late.phaseRemainMs;
            d.abortBlocks = late.abortBlocks;
            d.gateBlocks = late.gateBlocks;
            d.txFailures = late.txFailures;
            d.lastApState = late.lastApState;
            d.lastHandsOnState = late.lastHos;
            d.cadenceStable = late.cadenceStable;
            d.lateEchoEligible = late.lateEchoEligible;
            d.pendingEcho = late.pendingEcho;
            d.periodMs = late.periodMs;
            d.jitterMs = late.jitterMs;
            d.counterStep = late.counterStep;
            d.expectedNextCounter = late.expectedNextCounter;
            d.predictedNextRxMs = late.predictedNextRxMs;
            d.pendingSendAtMs = late.pendingSendAtMs;
            d.scheduledEchoes = late.scheduledEchoes;
            d.sentLateEchoes = late.sentLateEchoes;
            d.droppedLateEchoes = late.droppedLateEchoes;
            d.lateWindowMissed = late.lateWindowMissed;
            d.lastRxToTxMs = late.lastRxToTxMs;
            d.lastLeadMs = DashEpasLateEcho::kLateEchoLeadMs;
            d.preserveHandsOnLevel = late.preserveHandsOnLevel;
            d.lastSourceHandsOnLevel = late.lastSourceHandsOnLevel;
            d.lastTxHandsOnLevel = late.lastTxHandsOnLevel;
        }
        return d;
    }
    void resetReactiveCounters() override { nag.resetCounters(); }
    void setReactiveCounters(uint32_t ns, uint32_t rb, uint32_t pw, uint32_t es) override
    {
        nag.setCounters(ns, rb, pw, es);
    }
    void bumpReactiveCounters() override { nag.bumpCounters(); }

    const uint32_t *filterIds() const override
    {
        // 1080 added for UI_driverAssistAnonDebugParams visionSpeedSlider override.
        // 920 added for auto hardware detection (GTW_carConfig).
        // 880 (0x370 EPAS3P_sysStatus) added for EPAS-faithful nag engine.
        static constexpr uint32_t ids[] = {69, 280, 390, 760, 880, 920, 921, 1006, 1080, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 10; }

    void tick(uint32_t nowMs, CanDriver &driver) override
    {
        refreshLateNagEnabled();
        if (!lateEchoSelected())
            return;

        bool checkAdAllowed = !(checkAD && !checkAD());
        const char *gateReason = nullptr;
        if (!(bool)bionicSteering)
            gateReason = "toggle";
        else if (!APActive)
            gateReason = "apInactive";
        else if (!checkAdAllowed)
            gateReason = "checkAD";
        bool gatesActive = gateReason == nullptr;

        CanFrame echo;
        DashEpasLateEchoTxToken token;
        if (!lateNag.buildDueFrame(nowMs, echo, gatesActive, lastNagApState, lastNagHos, gateReason, token))
            return;
        if (!abortGuard.allowsInjection())
        {
            abortGuard.recordBlock(DashAbortGuardBlockPath::Nag);
            lateNag.notifyTxResult(token, false, nowMs);
            return;
        }
        bool ok = driver.send(echo);
        if (ok)
            framesSent++;
        lateNag.notifyTxResult(token, ok, nowMs);
    }

    void tick(CanDriver &driver) { tick(dashDiagNowMs(), driver); }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);
        updateHwDetectedFrom920(frame);
        if (frame.id == 880)
        {
            // TSL6P Burst NAG v4 (opt-in via bionicSteering; default OFF): bounded 0x370 echo burst.
            unsigned long nowMs = dashDiagNowMs();
            bool checkAdAllowed = !(checkAD && !checkAD());
            const char *gateReason = nullptr;
            if (!(bool)bionicSteering)
                gateReason = "toggle";
            else if (!APActive)
                gateReason = "apInactive";
            else if (!checkAdAllowed)
                gateReason = "checkAD";
            bool active = gateReason == nullptr;
            refreshLateNagEnabled();
            if (lateEchoSelected())
            {
                lateNag.onEpasFrame(frame, nowMs, active);
                return;
            }
            if (!legacyTsl6pSelected() || frame.dlc < 8)
                return;
            nag.advance(nowMs, active, gateReason);
            bool replayPending = nag.shouldEcho(nowMs);
            bool useReplay = replayPending && active;
            if (useReplay && !abortGuard.allowsInjection())
            {
                abortGuard.recordBlock(DashAbortGuardBlockPath::Nag);
                useReplay = false;
            }
            if (useReplay)
            {
                CanFrame echo;
                echo.id = 880;
                echo.dlc = 8;
                echo.data[0] = frame.data[0];
                echo.data[1] = frame.data[1];

                uint8_t d2lo = frame.data[2] & 0x0F;
                uint8_t d3 = frame.data[3];
                int signedBase = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
                nag.noteBaseTorqueRaw(signedBase);
                int target = nag.peekReplayDelta(nowMs);
                nag.applyToFrame(d2lo, d3, target);
                echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | d2lo);
                echo.data[3] = d3;

                echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40);
                echo.data[5] = frame.data[5];
                uint8_t cnt = static_cast<uint8_t>(frame.data[6] & 0x0F);
                cnt = static_cast<uint8_t>((cnt + 1) & 0x0F);
                echo.data[6] = static_cast<uint8_t>((frame.data[6] & 0xF0) | cnt);
                uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] +
                               echo.data[4] + echo.data[5] + echo.data[6];
                echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
                bool ok = driver.send(echo);
                if (ok)
                {
                    nag.commitReplayDelta(target);
                    framesSent++;
                    nag.notifyEchoSent();
                }
                else
                {
                    nag.failReplayTx(nowMs);
                }
            }
        }
        // STW_ACTN_RQ (0x045 = 69): Follow-Distance-Stalk as Source for Profile Mapping
        // byte[1]: 0x00=Pos1, 0x21=Pos2, 0x42=Pos3, 0x64=Pos4, 0x85=Pos5, 0xA6=Pos6, 0xC8=Pos7
        if (frame.id == 69)
        {
            if (frame.dlc < 2)
                return;
            if (!speedProfileAuto)
                return;
            uint8_t pos = frame.data[1] >> 5;
            if (pos <= 1)
                speedProfile = 2;
            else if (pos == 2)
                speedProfile = 1;
            else
                speedProfile = 0;
            return;
        }
        // UI_gpsVehicleSpeed (0x2F8 = 760): write UI_userSpeedOffset (bit40|6,
        // raw = kph+30). Byte 5 layout: bits 0-5 = offset (0-63), bit 6 reserved,
        // bit 7 = UI_userSpeedOffsetUnits (0=MPH, 1=KPH). We preserve bits 6-7
        // so the offset unit follows the car's setting.
        if (frame.id == 760)
        {
            uint32_t nowMs = dashDiagNowMs();
            legacySpeedDiag.gpsSpeedSeen = true;
            legacySpeedDiag.gpsSpeedFresh = true;
            if (frame.dlc >= 6)
            {
                legacySpeedDiag.gpsUserOffsetRaw = frame.data[5] & 0x3F;
                legacySpeedDiag.gpsUserOffsetKph = static_cast<int>(legacySpeedDiag.gpsUserOffsetRaw) - 30;
            }
            if (frame.dlc >= 7)
            {
                const uint8_t *bytes = frame.data;
                legacySpeedDiag.gpsMppLimitRaw = bytes[6] & 0x1F;
                legacySpeedDiag.gpsMppLimitKph = static_cast<uint16_t>(legacySpeedDiag.gpsMppLimitRaw) * 5U;
            }
            if (legacySpeedDiag.gpsSpeedLastMs != 0)
                legacySpeedDiag.gpsSpeedPeriodMs = nowMs - legacySpeedDiag.gpsSpeedLastMs;
            legacySpeedDiag.gpsSpeedLastMs = nowMs;
            ++legacySpeedDiag.gpsSpeedCount;
            legacySpeedDiag.blockedReason = "none";
            if (checkAD && !checkAD())
            {
                legacySpeedDiag.blockedReason = "checkAD";
                return;
            }
            if (frame.dlc < 6)
                return;

            LegacySmartOffsetMode smartMode = dashClampLegacySmartMode(static_cast<int>(legacySmartOffsetConfig.mode));
            uint8_t effectiveOffset = 0;
            if (smartMode == LegacySmartOffsetMode::Off)
            {
                legacySpeedDiag.result = LegacySmartOffsetResult{};
                legacySpeedDiag.result.mode = LegacySmartOffsetMode::Off;
                legacySpeedDiag.result.speedLimitRaw = fusedSpeedLimitRaw;
                legacySpeedDiag.result.lastUpdateMs = nowMs;
                effectiveOffset = dashComputeLegacySimpleOffsetKph((int)legacyOffset);
            }
            else
            {
                legacySpeedDiag.result = legacySmartOffsetEngine.compute(legacySmartOffsetConfig,
                                                                         fusedSpeedLimitRaw,
                                                                         nowMs,
                                                                         (bool)APActive || (bool)ADEnabled);
                effectiveOffset = legacySpeedDiag.result.outputOffsetKph;
            }

            if (effectiveOffset == 0)
                return;
            if (!abortGuard.allowsInjection())
            {
                legacySpeedDiag.blockedReason = "abortGuard";
                abortGuard.recordBlock(DashAbortGuardBlockPath::LegacySpeed0x2f8);
                return;
            }

            uint8_t raw = (uint8_t)(dashClampLegacySimpleOffsetKph(effectiveOffset) + 30);
            legacyFsdDiag.aux760.recordBefore(frame.data);
            frame.data[5] = (frame.data[5] & 0xC0) | (raw & 0x3F);
            legacyFsdDiag.aux760.recordAfter(frame.data);
            legacyFsdDiag.aux760.recordPath(FsdDiagBus::CanA, FsdDiagDriver::Twai);
            bool ok = driver.send(frame);
            legacySpeedDiag.lastSentOffsetRaw = static_cast<uint8_t>(raw & 0x3F);
            legacySpeedDiag.lastSentOffsetKph = effectiveOffset;
            if (ok)
            {
                framesSent++;
                legacySpeedDiag.txOk++;
                legacySpeedDiag.blockedReason = "none";
            }
            else
            {
                legacySpeedDiag.txFail++;
                legacySpeedDiag.blockedReason = "txFail";
            }
            legacyFsdDiag.recordMuxTx(legacyFsdDiag.aux760, ok, nowMs);
            if (onSend)
                onSend(0, ok);
            return;
        }
        // 0x438 (1080) — UI_driverAssistAnonDebugParams: visionSpeedSlider = 100
        if (frame.id == 1080)
        {
            if (frame.dlc < 8)
                return;
            if (!overrideSpeedLimit)
                return;
            if (checkAD && !checkAD())
                return;
            if (!abortGuard.allowsInjection())
            {
                abortGuard.recordBlock(DashAbortGuardBlockPath::LegacyVisionSlider0x438);
                return;
            }
            legacyFsdDiag.aux1080.recordBefore(frame.data);
            frame.data[7] = (frame.data[7] & 0x80) | 100;
            legacyFsdDiag.aux1080.recordAfter(frame.data);
            legacyFsdDiag.aux1080.recordPath(FsdDiagBus::CanA, FsdDiagDriver::Twai);
            framesSent++;
            bool ok = driver.send(frame);
            legacyFsdDiag.recordMuxTx(legacyFsdDiag.aux1080, ok, dashDiagNowMs());
            if (onSend)
                onSend(0, ok);
            return;
        }
        if (handleDISystemStatus(frame))
            return;
        if (handleDIVehicleStatus(frame))
            return;
        if (frame.id == 921)
        {
            if (frame.dlc < 1)
                return;
            uint8_t apState = readDASAutopilotStatus(frame);
            if (isPrimaryDasFrame(frame))
                abortGuard.onApState(apState, dashDiagNowMs());
            APActive = isDASAutopilotActive(apState);
            if (frame.dlc >= 2)
                fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);
            if (frame.dlc >= 6)
            {
                uint8_t hos = static_cast<uint8_t>((frame.data[5] >> 2) & 0x0F);
                bool checkAdAllowed = !(checkAD && !checkAD());
                const char *gateReason = nullptr;
                if (!(bool)bionicSteering)
                    gateReason = "toggle";
                else if (!APActive)
                    gateReason = "apInactive";
                else if (!checkAdAllowed)
                    gateReason = "checkAD";
                bool active = gateReason == nullptr;
                uint32_t nowMs = dashDiagNowMs();
                lastNagApState = apState;
                lastNagHos = hos;
                lastNagGateReason = gateReason;
                lastNagGatesActive = active;
                refreshLateNagEnabled();
                if (lateEchoSelected())
                    lateNag.onDasStatus(apState, hos, nowMs, active, gateReason);
                else if (legacyTsl6pSelected())
                    nag.onNagSample(hos, nowMs, active, apState, gateReason);
            }
            else if (lateEchoSelected() && (apState == 8 || apState == 9))
            {
                bool checkAdAllowed = !(checkAD && !checkAD());
                const char *gateReason = nullptr;
                if (!(bool)bionicSteering)
                    gateReason = "toggle";
                else if (!APActive)
                    gateReason = "apInactive";
                else if (!checkAdAllowed)
                    gateReason = "checkAD";
                bool active = gateReason == nullptr;
                uint32_t nowMs = dashDiagNowMs();
                lastNagApState = apState;
                lastNagGateReason = gateReason;
                lastNagGatesActive = active;
                refreshLateNagEnabled();
                lateNag.onDasStatus(apState, lastNagHos, nowMs, active, gateReason);
            }
            return;
        }
        // 0x3EE (1006) — FSD activation frame (mux 0/1)
        if (frame.id == 1006)
        {
            uint32_t nowMs = dashDiagNowMs();
            if (frame.dlc < 8)
            {
                legacyFsdDiag.mux0.lastSkip = FsdSkipReason::DlcShort;
                return;
            }

            auto index = readMuxID(frame);
            if (index == 0)
            {
                legacyFsdDiag.mux0.recordRx(nowMs);
                legacyFsdDiag.markMux0Rx(nowMs);
                legacyFsdDiag.mux0.recordPath(FsdDiagBus::CanA, FsdDiagDriver::Twai);
                bool forced = (bool)forceActivateRuntime;
                bool uiSelected = isFSDSelectedInUI(frame);
                fsdTriggered = forced || uiSelected;
                ADEnabled = (bool)fsdTriggered;
                legacyFsdDiag.triggered = (bool)fsdTriggered;
                legacyFsdDiag.forceRuntime = forced;
                legacyFsdDiag.triggerSource = forced ? FsdTriggerSource::Force : (uiSelected ? FsdTriggerSource::UiBit : FsdTriggerSource::FalseSource);

                if (!(bool)fsdTriggered)
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::NotTriggered;
                    legacyFsdDiag.health = FsdHealthState::NotTriggered;
                    return;
                }
                if (!abortGuard.allowsInjection())
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = FsdGateBlockReason::ApGate;
                    abortGuard.recordBlock(DashAbortGuardBlockPath::LegacyFsdMux0);
                    return;
                }
                bool legacyActivationAllowed = legacyFsdActivationAllowed
                                                   ? legacyFsdActivationAllowed(nowMs)
                                                   : injectionAllowed();
                if (!legacyActivationAllowed)
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = legacyFsdActivationAllowed
                                                      ? FsdGateBlockReason::LegacyFsdSettle
                                                      : currentGateBlockReason();
                    return;
                }

                // Plugin owns FSD activation frames: suppress built-in injection.
                if (!builtInFsdInjectionAllowed())
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    return;
                }

                legacyFsdDiag.mux0.recordBefore(frame.data);
                setBit(frame, 46, true);
                if (legacyFsdDiag.policy == LegacyFsdPolicy::TeslaParity ||
                    (legacyFsdDiag.policy == LegacyFsdPolicy::Experimental && legacyFsdDiag.profileWriteEnable))
                    setSpeedProfileV12V13(frame, speedProfile);
                legacyFsdDiag.mux0.recordAfter(frame.data);
                framesSent++;
                bool ok = driver.send(frame);
                legacyFsdDiag.recordMuxTx(legacyFsdDiag.mux0, ok, nowMs);
                legacyFsdDiag.markTxResult(ok, nowMs);
                if (onSend)
                    onSend(0, ok);
            }
            else if (index == 1)
            {
                legacyFsdDiag.mux1.recordRx(nowMs);
                legacyFsdDiag.markMux1Rx(nowMs);
                legacyFsdDiag.mux1.recordPath(FsdDiagBus::CanA, FsdDiagDriver::Twai);
                if (legacyFsdDiag.policy == LegacyFsdPolicy::Stable)
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::DisabledInStable;
                    return;
                }
                if (legacyFsdDiag.policy != LegacyFsdPolicy::TeslaParity && !legacyFsdDiag.mux1Enable)
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::DisabledByPolicy;
                    return;
                }
                if (!(bool)fsdTriggered)
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::NotTriggered;
                    legacyFsdDiag.health = FsdHealthState::NotTriggered;
                    return;
                }
                if (!abortGuard.allowsInjection())
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = FsdGateBlockReason::ApGate;
                    abortGuard.recordBlock(DashAbortGuardBlockPath::LegacyFsdMux1);
                    return;
                }
                if (!injectionAllowed())
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = currentGateBlockReason();
                    return;
                }

                // Plugin owns FSD activation frames: suppress built-in injection.
                if (!builtInFsdInjectionAllowed())
                {
                    legacyFsdDiag.mux1.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    return;
                }

                legacyFsdDiag.mux1.recordBefore(frame.data);
                setBit(frame, 19, false);
                if (legacyFsdDiag.visionLimitClearEnable)
                    setBit(frame, 48, false);
                legacyFsdDiag.mux1.recordAfter(frame.data);
                framesSent++;
                bool ok = driver.send(frame);
                legacyFsdDiag.recordMuxTx(legacyFsdDiag.mux1, ok, nowMs);
                legacyFsdDiag.markTxResult(ok, nowMs);
                if (onSend)
                    onSend(1, ok);
            }

            if (index == 0 && enablePrint)
            {
                char buf[LogRingBuffer::kMaxMsgLen];
                snprintf(buf, sizeof(buf), "LegacyHandler: AD: %d, Profile: %d",
                         (bool)ADEnabled, (int)speedProfile);
                logRing.push(buf,
#ifndef NATIVE_BUILD
                             millis()
#else
                             0
#endif
                );
#ifndef NATIVE_BUILD
                Serial.println(buf);
#endif
            }
        }
    }
};

struct HW3Handler : public CarManagerBase
{
    const uint32_t *filterIds() const override
    {
        // 880 (0x370 EPAS3P_sysStatus) + 923 (0x39B DAS_status Highland/HW4) added for EPAS-faithful nag engine.
        static constexpr uint32_t ids[] = {280, 390, 880, 920, 921, 923, 1016, 1021, 2047, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 10; }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);
        updateHwDetectedFrom920(frame);
        if (handleDISystemStatus(frame))
            return;
        if (handleDIVehicleStatus(frame))
            return;
        if (frame.id == 1016)
        {
            if (frame.dlc < 6)
                return;
            updateSummonFrom1016(frame);
            if (!speedProfileAuto)
                return;
            uint8_t followDistance = (frame.data[5] & 0b11100000) >> 5;
            switch (followDistance)
            {
            case 1:
                speedProfile = 2;
                break;
            case 2:
                speedProfile = 1;
                break;
            case 3:
                speedProfile = 0;
                break;
            default:
                break;
            }
            return;
        }
        if (frame.id == 921)
        {
            if (frame.dlc < 1)
                return;
            uint8_t apState = readDASAutopilotStatus(frame);
            abortGuard.onApState(apState, dashDiagNowMs());
            APActive = isDASAutopilotActive(apState);
            // Capture ISA fused speed limit from byte1[4:0]. raw*5 = kph;
            // 0 = SNA, 31 = NONE-broadcast — both treated as "unknown" by the
            // HW3 mux-2 override path. Used by dashComputeHw3OffsetRaw().
            if (frame.dlc >= 2)
                fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);
            // ISA chime suppress — runtime gate (spec Task 2, 对齐 HW4Handler:856-864)
            if ((bool)isaChimeSuppress && frame.dlc >= 8 && injectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw3DasStatus921))
            {
                frame.data[1] |= 0x20;
                frame.data[7] = computeVehicleChecksum(frame);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(0, true);
                return;
            }
            return;
        }
        if (frame.id == 2047)
        {
            if (frame.dlc < 6)
                return;
            if (readMuxID(frame) != 2)
                return;

            uint8_t next = readGTWAutopilot(frame);
            int prev = gatewayAutopilot;
            gatewayAutopilot = next;

            if (enablePrint && prev != next)
            {
                char buf[LogRingBuffer::kMaxMsgLen];
                snprintf(buf, sizeof(buf), "HW3Handler: GTW_autopilot: %d -> %u (%s)",
                         prev, (unsigned int)next, describeGTWAutopilot(next));
                logRing.push(buf,
#ifndef NATIVE_BUILD
                             millis()
#else
                             0
#endif
                );
#ifndef NATIVE_BUILD
                Serial.println(buf);
#endif
            }
            handleBanShield(frame, driver);
            return;
        }
        if (frame.id == 1021)
        {
            if (frame.dlc < 8)
                return;
            auto index = readMuxID(frame);

            // ── Mux 0: FSD activation ──────────────────────────────────────
            if (index == 0)
            {
                bool fsdRequested = (bool)forceActivateRuntime || isFSDSelectedInUI(frame);
                fsdTriggered = fsdRequested;
                ADEnabled = (bool)fsdTriggered;
            }
            if (index == 0 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw3FsdMux0))
            {
                speedOffset = std::max(std::min(((int)((frame.data[3] >> 1) & 0x3F) - 30) * 5, 100), 0);
                hw3StockOffsetKph = (int)speedOffset;
                setBit(frame, 46, true);
                if ((bool)tlsscBypass)
                    setBit(frame, 38, true);
                setSpeedProfileV12V13(frame, speedProfile);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(0, true);
            }

            // ── Mux 1: Nag suppression (bit-19 clear, legacy baseline) ──
            if (index == 1 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw3FsdMux1))
            {
                setBit(frame, 19, false);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(1, true);
            }

            // ── Mux 2: Speed offset (three-layer + slew limiter) ──────────
            if (index == 2 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw3FsdMux2))
            {
                uint8_t stockRaw = static_cast<uint8_t>(((frame.data[0] >> 6) & 0x03) |
                                                        ((frame.data[1] & 0x3F) << 2));
                uint8_t fl = fusedSpeedLimitRaw;
                int computeInput = (fl == 0 || fl == 31) ? (int)stockRaw : (int)speedOffset;
                uint8_t activeRaw = dashComputeHw3OffsetRaw(computeInput);

                if (fl == 0 || fl == 31)
                {
                    hw3OffsetTargetRaw = 0;
                    return;
                }
                hw3OffsetTargetRaw = activeRaw;

                // Slew limiter: damps downward drops only
                if (hw3OffsetSlew && fl > 0 && (int)fl * 5 < kHw3StockOffsetCutoverKph)
                {
                    uint32_t now =
#ifndef NATIVE_BUILD
                        millis();
#else
                        0;
#endif
                    uint8_t last = hw3OffsetLastRaw;
                    uint8_t ratePctPerSec = dashLoadHw3SlewRate(hw3SlewRate);
                    uint32_t rateRawPerSec = (uint32_t)ratePctPerSec * 4;
                    if (activeRaw < last && hw3OffsetLastSentMs != 0)
                    {
                        uint32_t dt = now - hw3OffsetLastSentMs;
                        uint32_t maxDrop = (rateRawPerSec * dt + 500) / 1000;
                        uint8_t floorRaw = last > maxDrop ? (uint8_t)(last - maxDrop) : 0;
                        if (activeRaw < floorRaw)
                        {
                            activeRaw = floorRaw;
                            hw3OffsetSlewCount = hw3OffsetSlewCount + 1;
                        }
                    }
                    hw3OffsetLastRaw = activeRaw;
                    hw3OffsetLastSentMs = now;
                }
                else
                {
                    hw3OffsetLastRaw = activeRaw;
#ifndef NATIVE_BUILD
                    hw3OffsetLastSentMs = millis();
#endif
                }

                // Write to wire format
                frame.data[0] &= ~(0b11000000);
                frame.data[1] &= ~(0b00111111);
                frame.data[0] |= (activeRaw & 0x03) << 6;
                frame.data[1] |= (activeRaw >> 2);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(2, true);
            }

            if (index == 0 && enablePrint)
            {
                char buf[LogRingBuffer::kMaxMsgLen];
                snprintf(buf, sizeof(buf), "HW3Handler: AD: %d, Profile: %d, Offset: %d",
                         (bool)ADEnabled, (int)speedProfile, (int)speedOffset);
                logRing.push(buf,
#ifndef NATIVE_BUILD
                             millis()
#else
                             0
#endif
                );
#ifndef NATIVE_BUILD
                Serial.println(buf);
#endif
            }
        }
    }
};

/**
 * NagHandler — Autosteer nag suppression (counter+1 echo method)
 *
 * Replicates the Chinese TSL6P module behavior:
 * - Listens for CAN 880 (0x370) = EPAS3P_sysStatus
 * - When handsOnLevel = 0 (nag would trigger):
 *   1. Copies the real frame
 *   2. Torque bytes (2,3): PASSTHROUGH by default (copied unchanged). Only when
 *      the opt-in runtime global nagTorqueTamperRuntime is set does it write
 *      byte 2 low nibble = 0x08 and byte 3 = 0xB6 (fixed 1.80 Nm). That tamper
 *      mode is the documented primary-suspect vector of the 2026-06-19 EPAS
 *      fault — opt-in only, never the default path.
 *   3. Sets byte 4 |= 0x40 (handsOnLevel = 1)
 *   4. Increments counter (byte 6 lower nibble + 1)
 *   5. Recalculates checksum (byte 7)
 * - The real EPAS frame with the same counter arrives AFTER -> rejected as duplicate
 *
 * Tested: Model Y Performance 2022 HW3, Basic Autopilot
 * Bus: X179 pin 2/3 (CAN bus 4)
 *
 * Enable with build flag: -D NAG_KILLER
 */
struct NagHandler : public CarManagerBase
{
    Shared<bool> nagKillerActive{true};
    Shared<uint32_t> nagEchoCount{0};

    const uint32_t *filterIds() const override
    {
        static constexpr uint32_t ids[] = {880, 920, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 3; }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);
        updateHwDetectedFrom920(frame);
        if (frame.id != 880 || frame.dlc < 8)
            return;

        uint8_t handsOn = (frame.data[4] >> 6) & 0x03;

        if (!nagKillerActive || !nagKillerRuntime || handsOn != 0)
            return;
        if (checkAD && !checkAD())
            return;

        CanFrame echo;
        echo.id = 880;
        echo.dlc = 8;

        // Bytes copied through unchanged in BOTH modes.
        echo.data[0] = frame.data[0];
        echo.data[1] = frame.data[1];
        echo.data[5] = frame.data[5];

        // Torque mode select (opt-in global; default false = PASSTHROUGH).
        // TORQUE_TAMPER (1.80 Nm fixed) is the documented primary-suspect vector
        // of the 2026-06-19 EPAS fault (docs/EPAS-NAG-REMOVAL-INCIDENT.md) —
        // opt-in only, never the default code path.
        if (nagTorqueTamperRuntime)
        {
            echo.data[2] = (frame.data[2] & 0xF0) | 0x08; // sign nibble positive
            echo.data[3] = 0xB6;                          // 1.80 Nm fixed torque
        }
        else
        {
            echo.data[2] = frame.data[2]; // PASSTHROUGH
            echo.data[3] = frame.data[3]; // PASSTHROUGH
        }

        // handsOnLevel = 1 (gate above guarantees bits 7:6 == 0)
        echo.data[4] = frame.data[4] | 0x40;

        // Counter + 1 (low nibble)
        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        // Checksum: sum(byte0..byte6) + 0x73
        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] +
                       echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        framesSent++;
        nagEchoCount++;
        driver.send(echo);

        if (enablePrint && (nagEchoCount % 500 == 1))
        {
            char buf[LogRingBuffer::kMaxMsgLen];
            snprintf(buf, sizeof(buf),
                     "NagHandler: echo=%u tamper=%s",
                     (unsigned int)(uint32_t)nagEchoCount,
                     (bool)nagTorqueTamperRuntime ? "ON" : "off");
            logRing.push(buf,
#ifndef NATIVE_BUILD
                         millis()
#else
                         0
#endif
            );
#ifndef NATIVE_BUILD
            Serial.println(buf);
#endif
        }
    }
};

struct HW4Handler : public CarManagerBase
{
    bool hw4Das923Byte1Moved = false;
    bool hw4Das923UseByte0 = false;
    uint8_t hw4Das923Byte0PinCount = 0;

    uint8_t readHw4Das923ApState(const CanFrame &frame)
    {
        uint8_t hw4State = static_cast<uint8_t>((frame.data[1] >> 4) & 0x0F);
        uint8_t byte0State = static_cast<uint8_t>(frame.data[0] & 0x0F);
        if (hw4State != 1U)
        {
            hw4Das923Byte1Moved = true;
            hw4Das923UseByte0 = false;
            hw4Das923Byte0PinCount = 0;
        }
        else if (!hw4Das923UseByte0 && !hw4Das923Byte1Moved)
        {
            if (byte0State >= 2U)
            {
                if (hw4Das923Byte0PinCount < 3U)
                    hw4Das923Byte0PinCount++;
                if (hw4Das923Byte0PinCount >= 3U)
                    hw4Das923UseByte0 = true;
            }
            else
            {
                hw4Das923Byte0PinCount = 0;
            }
        }
        return hw4Das923UseByte0 ? byte0State : hw4State;
    }

    const uint32_t *filterIds() const override
    {
        // 880 (0x370 EPAS3P_sysStatus) + 923 (0x39B DAS_status Highland/HW4) added for EPAS-faithful nag engine.
        static constexpr uint32_t ids[] = {280, 390, 880, 920, 921, 923, 1016, 1021, 2047, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 10; }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);
        updateHwDetectedFrom920(frame);
        if (handleDISystemStatus(frame))
            return;
        if (handleDIVehicleStatus(frame))
            return;
        if (frame.id == 921)
        {
            if (frame.dlc < 1)
                return;
            uint8_t apState = readDASAutopilotStatus(frame);
            abortGuard.onApState(apState, dashDiagNowMs());
            APActive = isDASAutopilotActive(apState);
            if (frame.dlc >= 2)
                fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);
            // ISA chime suppress — runtime gate (all build modes)
            if ((bool)isaChimeSuppress && frame.dlc >= 8 && injectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw4DasStatus921))
            {
                frame.data[1] |= 0x20;
                frame.data[7] = computeVehicleChecksum(frame);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(0, true);
                return;
            }
            return;
        }
        // ISA override — rewrite fused/vision speed limit to NONE (spec Task 3, 对齐 tesla handleDASStatusISAOverride)
        if (frame.id == 923)
        {
            if (frame.dlc < 2)
                return;
            uint8_t apState = readHw4Das923ApState(frame);
            abortGuard.onApState(apState, dashDiagNowMs());
            APActive = isDASAutopilotActive(apState);
            if (frame.dlc >= 2)
                fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);
            if (frame.dlc >= 8 && (bool)isaOverride && injectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw4DasStatus923))
            {
                frame.data[1] |= 0x1F; // DAS_fusedSpeedLimit = 31 (NONE)
                frame.data[2] |= 0x1F; // DAS_visionOnlySpeedLimit = 31 (NONE)
                frame.data[7] = computeVehicleChecksum(frame);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(0, true);
            }
            return;
        }
        if (frame.id == 1016)
        {
            if (frame.dlc < 6)
                return;
            updateSummonFrom1016(frame);
            if (!speedProfileAuto)
                return;
            auto fd = (frame.data[5] & 0b11100000) >> 5;
            switch (fd)
            {
            case 1:
                speedProfile = 3;
                break;
            case 2:
                speedProfile = 2;
                break;
            case 3:
                speedProfile = 1;
                break;
            case 4:
                speedProfile = 0;
                break;
            case 5:
                speedProfile = 4;
                break;
            }
            return;
        }
        if (frame.id == 2047)
        {
            if (frame.dlc < 6)
                return;
            if (readMuxID(frame) != 2)
                return;

            uint8_t next = readGTWAutopilot(frame);
            int prev = gatewayAutopilot;
            gatewayAutopilot = next;

            if (enablePrint && prev != next)
            {
                char buf[LogRingBuffer::kMaxMsgLen];
                snprintf(buf, sizeof(buf), "HW4Handler: GTW_autopilot: %d -> %u (%s)",
                         prev, (unsigned int)next, describeGTWAutopilot(next));
                logRing.push(buf,
#ifndef NATIVE_BUILD
                             millis()
#else
                             0
#endif
                );
#ifndef NATIVE_BUILD
                Serial.println(buf);
#endif
            }
            handleBanShield(frame, driver);
            return;
        }
        if (frame.id == 1021)
        {
            if (frame.dlc < 8)
                return;
            auto index = readMuxID(frame);

            // Mux 0: FSD activation
            if (index == 0)
            {
                bool fsdRequested = (bool)forceActivateRuntime || isFSDSelectedInUI(frame);
                fsdTriggered = fsdRequested;
                ADEnabled = (bool)fsdTriggered;
            }
            if (index == 0 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw4FsdMux0))
            {
                setBit(frame, 46, true);
                setBit(frame, 60, true);
                if ((bool)emergencyVehicleDetection)
                    setBit(frame, 59, true);
                if ((bool)tlsscBypass)
                    setBit(frame, 38, true);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(0, true);
            }

            // Mux 1: FSD-ready signal (bit 47) + nag suppression (bit 19).
            if (index == 1 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw4FsdMux1))
            {
                setBit(frame, 47, true);
                setBit(frame, 19, false);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(1, true);
            }

            // Mux 2: Speed profile + offset
            if (index == 2 && (bool)fsdTriggered && injectionAllowed() &&
                builtInFsdInjectionAllowed() &&
                abortGuardAllowsInjection(DashAbortGuardBlockPath::Hw4FsdMux2))
            {
                // Speed profile
                frame.data[7] &= static_cast<uint8_t>(~(0x07 << 4));
                frame.data[7] |= static_cast<uint8_t>((int)speedProfile & 0x07) << 4;
                // Offset (HW4 offset support)
                if ((int)hw4OffsetRaw > 0)
                    frame.data[1] = (frame.data[1] & 0xC0) | ((int)hw4OffsetRaw & 0x3F);
                framesSent++;
                driver.send(frame);
                if (onSend)
                    onSend(2, true);
            }

            if (index == 0 && enablePrint)
            {
                char buf[LogRingBuffer::kMaxMsgLen];
                snprintf(buf, sizeof(buf), "HW4Handler: AD: %d, Profile: %d",
                         (bool)ADEnabled, (int)speedProfile);
                logRing.push(buf,
#ifndef NATIVE_BUILD
                             millis()
#else
                             0
#endif
                );
#ifndef NATIVE_BUILD
                Serial.println(buf);
#endif
            }
        }
    }
};
