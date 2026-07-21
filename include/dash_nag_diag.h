#pragma once

#include <cstdint>

#include "dash_epas_late_echo.h"
#include "dash_nag_mode.h"
#include "dash_reactive_hold_nag.h"
#include "dash_reactive_nag.h"

inline const char *dashHumanReplayPhaseName(HumanReplayMode phase)
{
    switch (phase)
    {
    case HumanReplayMode::BURST_ON:
        return "burst_on";
    case HumanReplayMode::BURST_OFF:
        return "burst_off";
    case HumanReplayMode::COOLDOWN:
        return "cooldown";
    case HumanReplayMode::IDLE:
    default:
        return "idle";
    }
}

inline const char *dashLateEchoPhaseName(LateEchoModeState phase)
{
    switch (phase)
    {
    case LateEchoModeState::BURST_ON:
        return "burst_on";
    case LateEchoModeState::BURST_OFF:
        return "burst_off";
    case LateEchoModeState::COOLDOWN:
        return "cooldown";
    case LateEchoModeState::IDLE:
    default:
        return "idle";
    }
}

inline const char *dashReactiveHoldPhaseName(DashReactiveHoldPhase phase)
{
    switch (phase)
    {
    case DashReactiveHoldPhase::Proactive:
        return "proactive";
    case DashReactiveHoldPhase::Reactive:
        return "reactive";
    case DashReactiveHoldPhase::Idle:
    default:
        return "idle";
    }
}

inline DashReactiveDiag dashMapHumanReplayDiag(
    const DashReactiveDiag &source,
    uint32_t nowMs)
{
    static_cast<void>(nowMs);
    DashReactiveDiag out = source;
    out.selectedMode = dashNagModeToRaw(DashNagMode::HumanReplayTsl6p);
    out.selectedModeName = dashNagModeName(DashNagMode::HumanReplayTsl6p);
    out.runtimePhase = dashHumanReplayPhaseName(source.mode);
    return out;
}

inline DashReactiveDiag dashMapLateEchoDiag(
    const DashEpasLateEchoDiag &source,
    uint32_t nowMs)
{
    static_cast<void>(nowMs);
    DashReactiveDiag out;
    out.enabled = source.enabled;
    out.selectedMode = dashNagModeToRaw(DashNagMode::EpasLateEcho);
    out.selectedModeName = dashNagModeName(DashNagMode::EpasLateEcho);
    out.runtimePhase = dashLateEchoPhaseName(source.mode);
    out.injecting = source.mode == LateEchoModeState::BURST_ON;
    out.lastHandsOnState = source.lastHos;
    out.cooldownRemainMs = source.cooldownRemainMs;
    out.blockedReason = source.blockedReason;
    out.abortBlocks = source.abortBlocks;
    out.gateBlocks = source.gateBlocks;
    out.txFailures = source.txFailures;
    out.replayAttempts = source.replayAttempts;
    out.replaySuccesses = source.replaySuccesses;
    out.replayFailures = source.replayFailures;
    out.profileIndex = source.profileIndex;
    out.lastHosBefore = source.lastHosBefore;
    out.lastHosAfter = source.lastHosAfter;
    out.lastPeakRaw = source.lastTargetTorqueRaw;
    out.echoSent = source.sentLateEchoes;
    out.lastApState = source.lastApState;
    out.phaseRemainMs = source.phaseRemainMs;
    out.lateEchoMode = true;
    out.cadenceStable = source.cadenceStable;
    out.lateEchoEligible = source.lateEchoEligible;
    out.pendingEcho = source.pendingEcho;
    out.periodMs = source.periodMs;
    out.jitterMs = source.jitterMs;
    out.counterStep = source.counterStep;
    out.expectedNextCounter = source.expectedNextCounter;
    out.predictedNextRxMs = static_cast<uint32_t>(source.predictedNextRxMs);
    out.pendingSendAtMs = static_cast<uint32_t>(source.pendingSendAtMs);
    out.scheduledEchoes = source.scheduledEchoes;
    out.sentLateEchoes = source.sentLateEchoes;
    out.droppedLateEchoes = source.droppedLateEchoes;
    out.lateWindowMissed = source.lateWindowMissed;
    out.lastRxToTxMs = source.lastRxToTxMs;
    out.lastLeadMs = static_cast<int>(DashEpasLateEcho::kLateEchoLeadMs);
    out.preserveHandsOnLevel = source.preserveHandsOnLevel;
    out.lastSourceHandsOnLevel = source.lastSourceHandsOnLevel;
    out.lastTxHandsOnLevel = source.lastTxHandsOnLevel;
    return out;
}

inline DashReactiveDiag dashMapReactiveHoldDiag(
    const DashReactiveHoldDiag &source,
    uint32_t nowMs)
{
    static_cast<void>(nowMs);
    DashReactiveDiag out;
    out.selectedMode = dashNagModeToRaw(DashNagMode::ReactiveHold);
    out.selectedModeName = dashNagModeName(DashNagMode::ReactiveHold);
    out.runtimePhase = dashReactiveHoldPhaseName(source.phase);
    out.injecting = source.injecting;
    out.lastHandsOnState = source.lastHandsOnState;
    out.currentAmp = source.currentAmp;
    out.nagSamples = source.nagSamples;
    out.reactiveBursts = source.reactiveBursts;
    out.proactiveWiggles = source.proactiveWiggles;
    out.echoSent = source.echoSent;
    out.nextProactiveInMs = source.nextProactiveInMs;
    return out;
}

inline DashReactiveDiag dashMakeDisabledNagDiag()
{
    return {};
}
