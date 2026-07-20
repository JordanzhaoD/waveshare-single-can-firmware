#pragma once

#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "shared_types.h"
#include "log_buffer.h"
#include "dash_hw3_speed.h"
#include "dash_legacy_speed.h"
#include "dash_abort_guard.h"
#include "dash_nag_mode.h"
#include "dash_reactive_nag.h"
#include "dash_reactive_hold_nag.h"
#include "dash_nag_diag.h"
#include "dash_legacy_370_echo.h"
#include "dash_epas_late_echo.h"
#include "dash_ap_first_gate.h"
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

// Upstream v3.0.2-beta.8 built-in NAG modes. These values intentionally
// remain 0..3 so existing NVS/settings backups migrate without a schema bump.
enum class BuiltInNagMode : uint8_t
{
    Disabled = 0,
    ModeA = 1,
    ModeB = 2,
    ModeC = 3,
};

inline uint8_t clampNagMode(uint8_t mode)
{
    return mode <= static_cast<uint8_t>(BuiltInNagMode::ModeC)
               ? mode
               : static_cast<uint8_t>(BuiltInNagMode::Disabled);
}

inline const char *nagModeName(uint8_t mode)
{
    switch (static_cast<BuiltInNagMode>(clampNagMode(mode)))
    {
    case BuiltInNagMode::ModeA:
        return "Mode A";
    case BuiltInNagMode::ModeB:
        return "Mode B";
    case BuiltInNagMode::ModeC:
        return "Mode C";
    default:
        return "Off";
    }
}

inline constexpr float kNagTorqueNmMax = 1.80f;
inline constexpr float kNagTorqueNmMin = -1.80f;
inline constexpr uint16_t kNagTorqueRawMax = 0x08B6;
inline constexpr uint16_t kNagTorqueRawMin = 0x074E;

inline uint16_t clampNagTorqueRaw(uint16_t value)
{
    return std::max(kNagTorqueRawMin, std::min(kNagTorqueRawMax, value));
}

struct NagRuntimeDiag
{
    uint8_t mode = 0;
    uint32_t rxTarget = 0;
    uint32_t eligible = 0;
    uint32_t txOk = 0;
    uint32_t txFail = 0;
    uint32_t context399 = 0;
    uint32_t context129 = 0;
    uint8_t apState = 0;
    uint8_t handsOnState = 0;
    int16_t steeringAngleX10 = 0;
    uint16_t lastTorqueRaw = 0;
    uint8_t lastRxCounter = 0;
    uint8_t lastTxCounter = 0;
    const char *blockedReason = "off";
};

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
    LegacySpeedLimitSource limitSource = LegacySpeedLimitSource::None;
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
    uint8_t outputOffsetPct = 0;
    uint32_t mux0Count = 0;
    uint8_t mux0RxByte3 = 0;
    uint8_t mux0TxByte3 = 0;
    uint32_t txOk = 0;
    uint32_t txFail = 0;
    uint32_t offsetOnlyTxOk = 0;
    uint32_t offsetOnlyTxFail = 0;
    uint8_t configuredMode = 0;
    bool activationIndependent = true;
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
    virtual void observeApFirstState(uint8_t apState, uint32_t nowMs)
    {
        (void)apState;
        (void)nowMs;
    }
    virtual DashApFirstDecision decideApFirst(bool gateEnabled,
                                              bool instantEnabled,
                                              uint32_t debounceMs,
                                              uint32_t nowMs)
    {
        (void)gateEnabled;
        (void)instantEnabled;
        (void)debounceMs;
        (void)nowMs;
        return DashApFirstDecision{};
    }
    virtual void clearApFirstTiming() {}
    virtual void resetApFirstRuntime() {}
    virtual DashApFirstDiag apFirstDiag(uint32_t nowMs) const
    {
        (void)nowMs;
        return DashApFirstDiag{};
    }
    virtual void setNagMode(uint8_t mode) { (void)mode; }
    virtual DashReactiveDiag reactiveDiag() const { return dashMakeDisabledNagDiag(); }
    virtual DashReactiveDiag nagDiagForMode(DashNagMode /*mode*/) const
    {
        return dashMakeDisabledNagDiag();
    }
    // Instrumentation counter persistence (NVS round-trip) — survive power-off.
    virtual void resetNagCounters(DashNagMode /*mode*/) {}
    virtual void setNagCounters(DashNagMode /*mode*/, uint32_t /*ns*/, uint32_t /*rb*/,
                                uint32_t /*pw*/, uint32_t /*es*/) {}
    virtual void bumpNagCounters(DashNagMode /*mode*/) {}
    virtual bool nagCountersDirty(DashNagMode /*mode*/) const { return false; }
    virtual void markNagCountersPersisted(DashNagMode /*mode*/) {}
    virtual void resetReactiveCounters() { resetNagCounters(DashNagMode::HumanReplayTsl6p); }
    virtual void setReactiveCounters(uint32_t ns, uint32_t rb, uint32_t pw, uint32_t es)
    {
        setNagCounters(DashNagMode::HumanReplayTsl6p, ns, rb, pw, es);
    }
    virtual void bumpReactiveCounters() { bumpNagCounters(DashNagMode::HumanReplayTsl6p); }
    virtual ~CarManagerBase() = default;
};

struct LegacyHandler : public CarManagerBase
{
    DashReactiveNagBurst nag; // TSL6P replay burst state machine
    DashReactiveHoldNag reactiveHoldNag;
    DashEpasLateEcho lateNag;
    DashApFirstGate apFirstGate;
    DashNagMode nagMode{DashNagMode::Off};
    uint8_t lastNagApState{0};
    uint8_t lastNagHos{0};
    const char *lastNagGateReason{"toggle"};
    bool lastNagGatesActive{false};

    bool bionicDisabled() const override { return false; } // reactive has no auto-disable
    bool humanReplaySelected() const { return nagMode == DashNagMode::HumanReplayTsl6p; }
    bool lateEchoSelected() const { return nagMode == DashNagMode::EpasLateEcho; }
    bool reactiveHoldSelected() const { return nagMode == DashNagMode::ReactiveHold; }
    bool isPrimaryDasFrame(const CanFrame &frame) const { return frame.bus != CAN_BUS_PARTY; }

    bool legacySpeedOffsetRequested() const
    {
        return dashClampLegacySmartMode(static_cast<int>(legacySmartOffsetConfig.mode)) !=
               LegacySmartOffsetMode::Off;
    }

    uint8_t computeLegacySpeedOffset(uint32_t nowMs)
    {
        legacySpeedDiag.gpsSpeedFresh = legacySpeedDiag.gpsSpeedSeen &&
                                        static_cast<uint32_t>(nowMs - legacySpeedDiag.gpsSpeedLastMs) <=
                                            kLegacyGpsLimitFreshMs;
        uint8_t legacyLimitRaw = fusedSpeedLimitRaw;
        legacySpeedDiag.limitSource = dashLegacySmartLimitValid(fusedSpeedLimitRaw)
                                          ? LegacySpeedLimitSource::Fused
                                          : LegacySpeedLimitSource::None;
        if (legacySpeedDiag.gpsSpeedFresh &&
            dashLegacySmartLimitValid(legacySpeedDiag.gpsMppLimitRaw))
        {
            legacyLimitRaw = legacySpeedDiag.gpsMppLimitRaw;
            legacySpeedDiag.limitSource = LegacySpeedLimitSource::Gps2F8;
        }

        LegacySmartOffsetMode smartMode =
            dashClampLegacySmartMode(static_cast<int>(legacySmartOffsetConfig.mode));
        legacySpeedDiag.configuredMode = static_cast<uint8_t>(smartMode);
        if (smartMode == LegacySmartOffsetMode::Off)
        {
            legacySpeedDiag.result = LegacySmartOffsetResult{};
            legacySpeedDiag.result.mode = LegacySmartOffsetMode::Off;
            legacySpeedDiag.result.speedLimitRaw = legacyLimitRaw;
            legacySpeedDiag.result.speedLimitKph = dashLegacySmartLimitValid(legacyLimitRaw)
                                                       ? static_cast<uint16_t>(legacyLimitRaw) * 5U
                                                       : 0;
            legacySpeedDiag.result.lastUpdateMs = nowMs;
            uint8_t effectiveOffset = dashClampLegacySimpleOffsetKph((int)legacyOffset);
            if (dashLegacySmartLimitValid(legacyLimitRaw))
            {
                float limitKph = static_cast<float>(legacyLimitRaw) * 5.0f;
                float offsetKph = dashComputeOffset(limitKph, 0.05f);
                effectiveOffset = dashClampLegacySimpleOffsetKph(static_cast<int>(offsetKph + 0.5f));
            }
            legacySpeedDiag.result.outputOffsetKph = effectiveOffset;
            legacySpeedDiag.result.rawTargetKph =
                static_cast<uint16_t>(legacySpeedDiag.result.speedLimitKph + effectiveOffset);
            legacySpeedDiag.result.smoothedTargetKph = legacySpeedDiag.result.rawTargetKph;
            legacySpeedDiag.result.blockedReason = effectiveOffset > 0 ? "none" : "off";
            legacySpeedDiag.outputOffsetPct = legacySpeedDiag.result.speedLimitKph > 0
                                                  ? static_cast<uint8_t>(
                                                        (static_cast<uint32_t>(effectiveOffset) * 100U +
                                                         legacySpeedDiag.result.speedLimitKph / 2U) /
                                                        legacySpeedDiag.result.speedLimitKph)
                                                  : 0;
            legacySpeedDiag.blockedReason = effectiveOffset > 0 ? "ready" : "off";
            return effectiveOffset;
        }

        legacySpeedDiag.result = legacySmartOffsetEngine.compute(
            legacySmartOffsetConfig,
            legacyLimitRaw,
            nowMs,
            (bool)APActive || (bool)ADEnabled);
        uint8_t effectiveOffset = legacySpeedDiag.result.outputOffsetKph;
        legacySpeedDiag.outputOffsetPct = legacySpeedDiag.result.speedLimitKph > 0
                                              ? static_cast<uint8_t>(
                                                    (static_cast<uint32_t>(effectiveOffset) * 100U +
                                                     legacySpeedDiag.result.speedLimitKph / 2U) /
                                                    legacySpeedDiag.result.speedLimitKph)
                                              : 0;
        legacySpeedDiag.blockedReason = effectiveOffset > 0
                                            ? "ready"
                                            : legacySpeedDiag.result.blockedReason;
        return effectiveOffset;
    }

    void observeApFirstState(uint8_t apState, uint32_t nowMs) override
    {
        apFirstGate.observe(apState, nowMs);
    }

    DashApFirstDecision decideApFirst(bool gateEnabled,
                                      bool instantEnabled,
                                      uint32_t debounceMs,
                                      uint32_t nowMs) override
    {
        return apFirstGate.decide(gateEnabled, instantEnabled, debounceMs, nowMs);
    }

    void clearApFirstTiming() override { apFirstGate.clearTiming(); }
    void resetApFirstRuntime() override { apFirstGate.resetRuntime(); }
    DashApFirstDiag apFirstDiag(uint32_t nowMs) const override
    {
        return apFirstGate.diag(nowMs);
    }

    void refreshLateNagEnabled()
    {
        lateNag.setEnabled(lateEchoSelected() && (bool)bionicSteering);
    }

    void setNagMode(uint8_t mode) override
    {
        const DashNagMode nextMode = dashNagModeFromRaw(mode);
        if (nextMode != nagMode)
        {
            nag.reset();
            reactiveHoldNag.reset();
            nagMode = nextMode;
        }
        refreshLateNagEnabled();
    }

    void setNagModeForTest(const char *mode)
    {
        if (mode && std::strcmp(mode, "human_replay_tsl6p") == 0)
            setNagMode(static_cast<uint8_t>(DashNagMode::HumanReplayTsl6p));
        else if (mode && std::strcmp(mode, "late_echo") == 0)
            setNagMode(static_cast<uint8_t>(DashNagMode::EpasLateEcho));
        else if (mode && std::strcmp(mode, "reactive_hold") == 0)
            setNagMode(static_cast<uint8_t>(DashNagMode::ReactiveHold));
        else
            setNagMode(static_cast<uint8_t>(DashNagMode::Off));
    }

    void resetBionic(uint32_t seed) override
    {
        const uint32_t actualSeed = seed ? seed : 0xDEADBEEF;
        nag.reset();
        nag.init(actualSeed);
        reactiveHoldNag.init(actualSeed);
        lateNag = DashEpasLateEcho{};
        refreshLateNagEnabled();
    }

    DashEpasLateEchoDiag lateEchoDiag(uint32_t nowMs) const { return lateNag.diag(nowMs); }

    DashReactiveDiag nagDiagForMode(DashNagMode mode) const override
    {
        const uint32_t nowMs = dashDiagNowMs();
        switch (mode)
        {
        case DashNagMode::HumanReplayTsl6p:
            return dashMapHumanReplayDiag(nag.diag(nowMs), nowMs);
        case DashNagMode::EpasLateEcho:
            return dashMapLateEchoDiag(lateNag.diag(nowMs), nowMs);
        case DashNagMode::ReactiveHold:
            return dashMapReactiveHoldDiag(reactiveHoldNag.diag(nowMs), nowMs);
        case DashNagMode::Off:
        default:
            return dashMakeDisabledNagDiag();
        }
    }

    DashReactiveDiag reactiveDiag() const override
    {
        switch (nagMode)
        {
        case DashNagMode::HumanReplayTsl6p:
        case DashNagMode::EpasLateEcho:
        case DashNagMode::ReactiveHold:
        {
            DashReactiveDiag d = nagDiagForMode(nagMode);
            d.enabled = (bool)bionicSteering;
            return d;
        }
        case DashNagMode::Off:
        default:
            return dashMakeDisabledNagDiag();
        }
    }

    void resetNagCounters(DashNagMode mode) override
    {
        if (mode == DashNagMode::HumanReplayTsl6p)
            nag.resetCounters();
        else if (mode == DashNagMode::ReactiveHold)
            reactiveHoldNag.resetCounters();
    }

    void setNagCounters(DashNagMode mode, uint32_t ns, uint32_t rb,
                        uint32_t pw, uint32_t es) override
    {
        if (mode == DashNagMode::HumanReplayTsl6p)
            nag.setCounters(ns, rb, pw, es);
        else if (mode == DashNagMode::ReactiveHold)
            reactiveHoldNag.setCounters(ns, rb, pw, es);
    }

    void bumpNagCounters(DashNagMode mode) override
    {
        if (mode == DashNagMode::HumanReplayTsl6p)
            nag.bumpCounters();
        else if (mode == DashNagMode::ReactiveHold)
            reactiveHoldNag.bumpCounters();
    }

    bool nagCountersDirty(DashNagMode mode) const override
    {
        if (mode == DashNagMode::HumanReplayTsl6p)
            return nag.countersDirty();
        if (mode == DashNagMode::ReactiveHold)
            return reactiveHoldNag.countersDirty();
        return false;
    }

    void markNagCountersPersisted(DashNagMode mode) override
    {
        if (mode == DashNagMode::HumanReplayTsl6p)
            nag.markCountersPersisted();
        else if (mode == DashNagMode::ReactiveHold)
            reactiveHoldNag.markCountersPersisted();
    }

    const uint32_t *filterIds() const override
    {
        // 1080 added for UI_driverAssistAnonDebugParams visionSpeedSlider override.
        // 920 added for auto hardware detection (GTW_carConfig).
        // 880 (0x370 EPAS3P_sysStatus) added for EPAS-faithful nag engine.
        static constexpr uint32_t ids[] = {69, 280, 297, 390, 760, 880, 920, 921, 1006, 1080, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 11; }

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
            if (frame.dlc < 8)
                return;
            if (reactiveHoldSelected())
            {
                if (!active || !reactiveHoldNag.shouldEcho(nowMs))
                    return;
                if (!abortGuard.allowsInjection())
                {
                    abortGuard.recordBlock(DashAbortGuardBlockPath::Nag);
                    return;
                }

                const int hold = reactiveHoldNag.computeHold(nowMs);
                uint8_t d2lo = frame.data[2] & 0x0F;
                uint8_t d3 = frame.data[3];
                reactiveHoldNag.applyToFrame(d2lo, d3, hold);
                CanFrame echo = dashBuildLegacy370Echo(frame, d2lo, d3, true);
                if (driver.send(echo))
                {
                    framesSent++;
                    reactiveHoldNag.notifyEchoSent();
                }
                return;
            }
            if (!humanReplaySelected())
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
                uint8_t d2lo = frame.data[2] & 0x0F;
                uint8_t d3 = frame.data[3];
                int signedBase = DashReactiveNagBurst::decodeSignedTorque(d2lo, d3);
                nag.noteBaseTorqueRaw(signedBase);
                int target = nag.peekReplayDelta(nowMs);
                nag.applyToFrame(d2lo, d3, target);
                CanFrame echo = dashBuildLegacy370Echo(frame, d2lo, d3, true);
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
        // UI_gpsVehicleSpeed (0x2F8 = 760) is read-only in the recovered
        // reference path. Its UI_mppSpeedLimit becomes the preferred Legacy
        // limit source; the actual offset is written on gated 0x3EE mux 0.
        if (frame.id == 760)
        {
            if (frame.dlc < 7)
                return;
            uint32_t nowMs = dashDiagNowMs();
            legacySpeedDiag.gpsSpeedSeen = true;
            legacySpeedDiag.gpsSpeedFresh = true;
            legacySpeedDiag.gpsUserOffsetRaw = frame.data[5] & 0x3F;
            legacySpeedDiag.gpsUserOffsetKph = static_cast<int>(legacySpeedDiag.gpsUserOffsetRaw) - 30;
            legacySpeedDiag.gpsMppLimitRaw = frame.data[6] & 0x1F;
            legacySpeedDiag.gpsMppLimitKph = static_cast<uint16_t>(legacySpeedDiag.gpsMppLimitRaw) * 5U;
            if (legacySpeedDiag.gpsSpeedLastMs != 0)
                legacySpeedDiag.gpsSpeedPeriodMs = nowMs - legacySpeedDiag.gpsSpeedLastMs;
            legacySpeedDiag.gpsSpeedLastMs = nowMs;
            ++legacySpeedDiag.gpsSpeedCount;
            legacySpeedDiag.blockedReason = "none";
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
            {
                const uint32_t apNowMs = dashDiagNowMs();
                abortGuard.onApState(apState, apNowMs);
                observeApFirstState(apState, apNowMs);
            }
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
                else if (humanReplaySelected())
                    nag.onNagSample(hos, nowMs, active, apState, gateReason);
                else if (reactiveHoldSelected())
                    reactiveHoldNag.onNagSample(hos, nowMs, active);
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

                const bool speedOffsetRequested = legacySpeedOffsetRequested();
                if (!(bool)fsdTriggered && !speedOffsetRequested)
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::NotTriggered;
                    legacyFsdDiag.health = FsdHealthState::NotTriggered;
                    legacySpeedDiag.blockedReason = "off";
                    return;
                }
                if (!abortGuard.allowsInjection())
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = FsdGateBlockReason::ApGate;
                    legacySpeedDiag.blockedReason = "abortGuard";
                    abortGuard.recordBlock(speedOffsetRequested
                                               ? DashAbortGuardBlockPath::LegacySpeed0x2f8
                                               : DashAbortGuardBlockPath::LegacyFsdMux0);
                    return;
                }
                bool activationAllowed = false;
                if ((bool)fsdTriggered)
                {
                    activationAllowed = legacyFsdActivationAllowed
                                            ? legacyFsdActivationAllowed(nowMs)
                                            : injectionAllowed();
                    if (!activationAllowed)
                    {
                        legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                        legacyFsdDiag.health = FsdHealthState::GateBlocked;
                        legacyFsdDiag.lastBlockedBy = legacyFsdActivationAllowed
                                                          ? FsdGateBlockReason::LegacyFsdSettle
                                                          : currentGateBlockReason();
                        if (!speedOffsetRequested)
                            return;
                    }
                }
                if (checkAD && !checkAD())
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacyFsdDiag.lastBlockedBy = currentGateBlockReason();
                    legacySpeedDiag.blockedReason = "checkAD";
                    return;
                }

                // Plugin owns FSD activation frames: suppress built-in injection.
                if (!builtInFsdInjectionAllowed())
                {
                    legacyFsdDiag.mux0.lastSkip = FsdSkipReason::GateBlocked;
                    legacyFsdDiag.health = FsdHealthState::GateBlocked;
                    legacySpeedDiag.blockedReason = "pluginOwner";
                    return;
                }

                legacyFsdDiag.mux0.recordBefore(frame.data);
                if (activationAllowed)
                    setBit(frame, 46, true);
                uint8_t effectiveOffset = (speedOffsetRequested || (bool)fsdTriggered)
                                              ? computeLegacySpeedOffset(nowMs)
                                              : 0;
                if (effectiveOffset > 0)
                    dashWriteLegacyOffsetTo3eeMux0(frame.data, effectiveOffset);
                if (activationAllowed &&
                    (legacyFsdDiag.policy == LegacyFsdPolicy::TeslaParity ||
                     (legacyFsdDiag.policy == LegacyFsdPolicy::Experimental && legacyFsdDiag.profileWriteEnable)))
                    setSpeedProfileV12V13(frame, speedProfile);

                // A configured Auto/Custom mode with no valid limit must fail
                // closed instead of retransmitting an unchanged mux-0 frame.
                if (!activationAllowed && effectiveOffset == 0)
                    return;
                legacySpeedDiag.mux0Count++;
                legacySpeedDiag.mux0RxByte3 = legacyFsdDiag.mux0.before[3];
                legacySpeedDiag.mux0TxByte3 = frame.data[3];
                legacyFsdDiag.mux0.recordAfter(frame.data);
                framesSent++;
                bool ok = driver.send(frame);
                if (effectiveOffset > 0)
                {
                    legacySpeedDiag.lastSentOffsetRaw =
                        static_cast<uint8_t>(dashClampLegacySmartOffsetKph(effectiveOffset) + 30U);
                    legacySpeedDiag.lastSentOffsetKph = effectiveOffset;
                    if (ok)
                    {
                        legacySpeedDiag.txOk++;
                        legacySpeedDiag.blockedReason = "none";
                    }
                    else
                    {
                        legacySpeedDiag.txFail++;
                        legacySpeedDiag.blockedReason = "txFail";
                    }
                }
                if (activationAllowed)
                {
                    legacyFsdDiag.recordMuxTx(legacyFsdDiag.mux0, ok, nowMs);
                    legacyFsdDiag.markTxResult(ok, nowMs);
                }
                else if (ok)
                {
                    legacySpeedDiag.offsetOnlyTxOk++;
                }
                else
                {
                    legacySpeedDiag.offsetOnlyTxFail++;
                }
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
        static constexpr uint32_t ids[] = {280, 297, 390, 880, 920, 921, 923, 1016, 1021, 2047, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 11; }

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
 * Upstream v3.0.2-beta.8 built-in NAG suppression.
 *
 * This handler is intentionally independent from the selected vehicle
 * handler. Dashboard code feeds it the original Party-CAN frames after the
 * normal Legacy/HW3/HW4 path, so changing vehicle generation cannot silently
 * disconnect NAG processing.
 */
struct NagHandler : public CarManagerBase
{
    Shared<bool> nagKillerActive{true};
    Shared<uint8_t> nagMode{static_cast<uint8_t>(BuiltInNagMode::ModeA)};
    Shared<uint32_t> nagEchoCount{0};

    static constexpr uint32_t kTargetId = 0x370;
    static constexpr uint32_t kApStateId = 0x399;
    static constexpr uint32_t kSteeringId = 0x129;
    static constexpr uint32_t kContextFreshMs = 1000;
    static constexpr uint32_t kModeBBurstMs = 1000;
    static constexpr uint32_t kModeBPauseMs = 1500;
    static constexpr uint32_t kModeBTorqueStepMs = 200;

    void setMode(uint8_t mode) { nagMode = clampNagMode(mode); }

    const uint32_t *filterIds() const override
    {
        static constexpr uint32_t ids[] = {kTargetId};
        return ids;
    }
    uint8_t filterIdCount() const override { return 1; }

    const uint32_t *modeFilterIds() const
    {
        static constexpr uint32_t ids[] = {kTargetId, kApStateId, kSteeringId};
        return ids;
    }

    uint8_t modeFilterIdCount(uint8_t mode) const
    {
        return clampNagMode(mode) == static_cast<uint8_t>(BuiltInNagMode::ModeC) ? 3 : 1;
    }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        handleMessageAt(frame, driver, dashDiagNowMs());
    }

    void handleMessageAt(CanFrame &frame, CanDriver &driver, uint32_t now)
    {
        uint8_t selectedMode = clampNagMode(nagMode);
        if (selectedMode != activeMode_)
            resetModeState(selectedMode, now);

        if (frame.id == kApStateId)
        {
            updateApState(frame, now);
            return;
        }
        if (frame.id == kSteeringId)
        {
            updateSteering(frame, now);
            return;
        }
        if (frame.id != kTargetId)
            return;

        rxTarget_++;
        if (onFrame)
            onFrame(frame);
        if (frame.dlc < 8)
        {
            blockedReason_ = "dlc";
            return;
        }

        uint8_t handsOn = (frame.data[4] >> 6) & 0x03;
        uint16_t torqueRaw = static_cast<uint16_t>((frame.data[2] & 0x0F) << 8) |
                             frame.data[3];
        bool isOwnEcho = (handsOn == 1 && torqueRaw == kNagTorqueRawMax) ||
                         matchesLastEcho(frame);

        if (!nagKillerActive || !nagKillerRuntime || selectedMode == 0)
        {
            blockedReason_ = "off";
            return;
        }
        if (isOwnEcho)
        {
            blockedReason_ = "ownEcho";
            return;
        }
        if (handsOn != 0)
        {
            blockedReason_ = "handsOn";
            return;
        }

        eligible_++;
        uint16_t torque = kNagTorqueRawMax;
        bool setHandsOn = true;
        if (!decideInjection(selectedMode, now, torque, setHandsOn))
            return;

        CanFrame echo{};
        echo.id = kTargetId;
        echo.dlc = 8;
        echo.bus = frame.bus;

        echo.data[0] = frame.data[0];
        echo.data[1] = frame.data[1];
        torque = clampNagTorqueRaw(torque);
        echo.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) |
                                            ((torque >> 8) & 0x0F));
        echo.data[3] = static_cast<uint8_t>(torque & 0xFF);
        echo.data[5] = frame.data[5];
        echo.data[4] = setHandsOn ? static_cast<uint8_t>(frame.data[4] | 0x40)
                                  : static_cast<uint8_t>(frame.data[4] & ~0xC0);

        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] +
                       echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        bool ok = driver.send(echo);
        lastTorqueRaw_ = torque;
        lastRxCounter_ = static_cast<uint8_t>(frame.data[6] & 0x0F);
        lastTxCounter_ = cnt;
        if (ok)
        {
            framesSent++;
            nagEchoCount++;
            txOk_++;
            lastEcho_ = echo;
            lastEchoValid_ = true;
            blockedReason_ = "none";
        }
        else
        {
            txFail_++;
            blockedReason_ = "txFail";
        }
        if (onSend)
            onSend(0, ok);

        if (enablePrint && (nagEchoCount % 500 == 1))
        {
            char buf[LogRingBuffer::kMaxMsgLen];
            snprintf(buf, sizeof(buf), "NagHandler: mode=%s echo=%u",
                     nagModeName(selectedMode),
                     (unsigned int)(uint32_t)nagEchoCount);
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

    NagRuntimeDiag diag() const
    {
        NagRuntimeDiag d;
        d.mode = clampNagMode(nagMode);
        d.rxTarget = rxTarget_;
        d.eligible = eligible_;
        d.txOk = txOk_;
        d.txFail = txFail_;
        d.context399 = context399_;
        d.context129 = context129_;
        d.apState = apState_;
        d.handsOnState = handsOnState_;
        d.steeringAngleX10 = steeringAngleX10_;
        d.lastTorqueRaw = lastTorqueRaw_;
        d.lastRxCounter = lastRxCounter_;
        d.lastTxCounter = lastTxCounter_;
        d.blockedReason = blockedReason_;
        return d;
    }

private:
    uint8_t activeMode_ = 0xFF;
    uint8_t modeBTorqueIndex_ = 0;
    uint32_t modeEnteredMs_ = 0;
    uint32_t modeBLastStepMs_ = 0;
    uint8_t apState_ = 0;
    uint8_t handsOnState_ = 0;
    int16_t steeringAngleX10_ = 0;
    uint32_t lastApStateMs_ = 0;
    uint32_t lastSteeringMs_ = 0;
    uint32_t handsOnStateEnteredMs_ = 0;
    bool apStateSeen_ = false;
    bool steeringSeen_ = false;
    bool handsOnStateSeen_ = false;
    uint16_t walkSeed_ = 0;
    float lastModeCTorqueNm_ = 0.0f;
    CanFrame lastEcho_{};
    bool lastEchoValid_ = false;
    uint32_t rxTarget_ = 0;
    uint32_t eligible_ = 0;
    uint32_t txOk_ = 0;
    uint32_t txFail_ = 0;
    uint32_t context399_ = 0;
    uint32_t context129_ = 0;
    uint16_t lastTorqueRaw_ = 0;
    uint8_t lastRxCounter_ = 0;
    uint8_t lastTxCounter_ = 0;
    const char *blockedReason_ = "off";

    static uint16_t torqueNmToRaw(float torqueNm)
    {
        torqueNm = std::max(kNagTorqueNmMin, std::min(kNagTorqueNmMax, torqueNm));
        float scaled = (torqueNm + 20.5f) * 100.0f + 0.5f;
        return clampNagTorqueRaw(static_cast<uint16_t>(scaled));
    }

    void resetModeState(uint8_t mode, uint32_t now)
    {
        activeMode_ = mode;
        modeBTorqueIndex_ = 0;
        modeEnteredMs_ = now;
        modeBLastStepMs_ = now;
        apState_ = 0;
        handsOnState_ = 0;
        steeringAngleX10_ = 0;
        lastApStateMs_ = 0;
        lastSteeringMs_ = 0;
        handsOnStateEnteredMs_ = 0;
        apStateSeen_ = false;
        steeringSeen_ = false;
        handsOnStateSeen_ = false;
        walkSeed_ = 0;
        lastModeCTorqueNm_ = 0.0f;
        lastEchoValid_ = false;
        blockedReason_ = mode == 0 ? "off" : "waiting";
    }

    bool matchesLastEcho(const CanFrame &frame) const
    {
        if (!lastEchoValid_ || frame.id != lastEcho_.id || frame.dlc != lastEcho_.dlc)
            return false;
        return std::equal(frame.data, frame.data + frame.dlc, lastEcho_.data);
    }

    void updateApState(const CanFrame &frame, uint32_t now)
    {
        if (frame.dlc < 8)
        {
            blockedReason_ = "context399Dlc";
            return;
        }
        uint8_t apState = static_cast<uint8_t>((frame.data[0] >> 4) & 0x0F);
        uint8_t handsOnState = static_cast<uint8_t>(frame.data[0] & 0x0F);
        apState_ = apState;
        lastApStateMs_ = now;
        apStateSeen_ = true;
        context399_++;
        if (!handsOnStateSeen_ || handsOnState != handsOnState_)
        {
            handsOnState_ = handsOnState;
            handsOnStateEnteredMs_ = now;
            handsOnStateSeen_ = true;
        }
    }

    void updateSteering(const CanFrame &frame, uint32_t now)
    {
        if (frame.dlc < 8)
        {
            blockedReason_ = "context129Dlc";
            return;
        }
        steeringAngleX10_ = static_cast<int16_t>((static_cast<uint16_t>(frame.data[1]) << 8) |
                                                 frame.data[0]);
        lastSteeringMs_ = now;
        steeringSeen_ = true;
        context129_++;
    }

    bool decideInjection(uint8_t selectedMode, uint32_t now, uint16_t &torque,
                         bool &setHandsOn)
    {
        if (selectedMode == static_cast<uint8_t>(BuiltInNagMode::ModeA))
        {
            torque = kNagTorqueRawMax;
            setHandsOn = true;
            return true;
        }

        if (selectedMode == static_cast<uint8_t>(BuiltInNagMode::ModeB))
        {
            constexpr uint16_t kModeBTorques[] = {0x08B6, 0x0898, 0x076C, 0x074E};
            constexpr uint32_t kCycleMs = kModeBBurstMs + kModeBPauseMs;
            if ((now - modeEnteredMs_) % kCycleMs >= kModeBBurstMs)
            {
                blockedReason_ = "modeBPause";
                return false;
            }
            if (now - modeBLastStepMs_ >= kModeBTorqueStepMs)
            {
                modeBTorqueIndex_ = static_cast<uint8_t>((modeBTorqueIndex_ + 1) % 4);
                modeBLastStepMs_ = now;
            }
            torque = kModeBTorques[modeBTorqueIndex_];
            setHandsOn = true;
            return true;
        }

        if (selectedMode != static_cast<uint8_t>(BuiltInNagMode::ModeC))
        {
            blockedReason_ = "off";
            return false;
        }
        if (!apStateSeen_ || !handsOnStateSeen_)
        {
            blockedReason_ = "no399";
            return false;
        }
        if (!steeringSeen_)
        {
            blockedReason_ = "no129";
            return false;
        }
        if (now - lastApStateMs_ > kContextFreshMs)
        {
            blockedReason_ = "stale399";
            return false;
        }
        if (now - lastSteeringMs_ > kContextFreshMs)
        {
            blockedReason_ = "stale129";
            return false;
        }
        if (apState_ < 3 || apState_ > 6)
        {
            blockedReason_ = "apState";
            return false;
        }
        if (steeringAngleX10_ < -50 || steeringAngleX10_ > 50)
        {
            blockedReason_ = "steeringAngle";
            return false;
        }

        float torqueNm = 0.0f;
        if (handsOnState_ == 2)
        {
            if (now - handsOnStateEnteredMs_ < 2000)
            {
                blockedReason_ = "state2Delay";
                return false;
            }
            walkSeed_ = static_cast<uint16_t>(walkSeed_ * 1103u + 12345u);
            float delta = (static_cast<int>(walkSeed_ & 0x1F) - 16) * 0.05f;
            float magnitude = std::fabs(lastModeCTorqueNm_) + delta;
            magnitude = std::max(0.5f, std::min(kNagTorqueNmMax, magnitude));
            torqueNm = steeringAngleX10_ > 0 ? -magnitude : magnitude;
            lastModeCTorqueNm_ = torqueNm;
        }
        else if (handsOnState_ == 3)
        {
            if (now - handsOnStateEnteredMs_ < 1000)
            {
                blockedReason_ = "state3Delay";
                return false;
            }
            uint32_t phase = (now - handsOnStateEnteredMs_ - 1000) % 1000;
            torqueNm = phase < 500 ? -1.8f + (phase / 500.0f) * 3.6f
                                   : 1.8f - ((phase - 500) / 500.0f) * 3.6f;
        }
        else
        {
            blockedReason_ = "handsOnState";
            return false;
        }

        torque = torqueNmToRaw(torqueNm);
        setHandsOn = std::fabs(torqueNm) >= 1.0f;
        return true;
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
        static constexpr uint32_t ids[] = {280, 297, 390, 880, 920, 921, 923, 1016, 1021, 2047, CAN_ID_OTA_STATUS};
        return ids;
    }
    uint8_t filterIdCount() const override { return 11; }

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
