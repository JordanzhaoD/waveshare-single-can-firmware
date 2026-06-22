#pragma once

#include <cstdint>
#include <cstring>

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif
#endif

enum class LegacyFsdPolicy : uint8_t
{
    Stable = 0,
    Experimental = 1,
    TeslaParity = 2,
};

enum class FsdTriggerSource : uint8_t
{
    Unknown = 0,
    Force = 1,
    UiBit = 2,
    FalseSource = 3,
};

enum class FsdSkipReason : uint8_t
{
    None = 0,
    Sent = 1,
    DisabledInStable = 2,
    DisabledByPolicy = 3,
    NotTriggered = 4,
    GateBlocked = 5,
    TxFail = 6,
    DlcShort = 7,
};

enum class FsdGateBlockReason : uint8_t
{
    None = 0,
    CheckAd = 1,
    CanActive = 2,
    Ota = 3,
    ApGate = 4,
    CompileGate = 5,
    LegacyFsdSettle = 6,
};

enum class FsdHealthState : uint8_t
{
    Unknown = 0,
    Healthy = 1,
    NoSourceFrame = 2,
    NotTriggered = 3,
    GateBlocked = 4,
    TxDegraded = 5,
    TxOkVehicleUnconfirmed = 6,
};

enum class FsdDiagBus : uint8_t
{
    Unknown = 0,
    CanA = 1,
    CanB = 2,
};

enum class FsdDiagDriver : uint8_t
{
    Unknown = 0,
    Twai = 1,
    Mcp2515 = 2,
};

static inline const char *legacyFsdPolicyName(LegacyFsdPolicy policy)
{
    switch (policy)
    {
    case LegacyFsdPolicy::Stable:
        return "legacy_stable";
    case LegacyFsdPolicy::Experimental:
        return "legacy_experimental";
    case LegacyFsdPolicy::TeslaParity:
        return "legacy_tesla_parity";
    }
    return "legacy_stable";
}

static inline const char *fsdTriggerSourceName(FsdTriggerSource source)
{
    switch (source)
    {
    case FsdTriggerSource::Unknown:
        return "unknown";
    case FsdTriggerSource::Force:
        return "force";
    case FsdTriggerSource::UiBit:
        return "ui_bit";
    case FsdTriggerSource::FalseSource:
        return "false";
    }
    return "unknown";
}

static inline const char *fsdSkipReasonName(FsdSkipReason reason)
{
    switch (reason)
    {
    case FsdSkipReason::None:
        return "none";
    case FsdSkipReason::Sent:
        return "sent";
    case FsdSkipReason::DisabledInStable:
        return "disabled_in_stable";
    case FsdSkipReason::DisabledByPolicy:
        return "disabled_by_policy";
    case FsdSkipReason::NotTriggered:
        return "not_triggered";
    case FsdSkipReason::GateBlocked:
        return "gate_blocked";
    case FsdSkipReason::TxFail:
        return "tx_fail";
    case FsdSkipReason::DlcShort:
        return "dlc_short";
    }
    return "none";
}

static inline const char *fsdGateBlockReasonName(FsdGateBlockReason reason)
{
    switch (reason)
    {
    case FsdGateBlockReason::None:
        return "none";
    case FsdGateBlockReason::CheckAd:
        return "check_ad";
    case FsdGateBlockReason::CanActive:
        return "can_active";
    case FsdGateBlockReason::Ota:
        return "ota";
    case FsdGateBlockReason::ApGate:
        return "ap_gate";
    case FsdGateBlockReason::CompileGate:
        return "compile_gate";
    case FsdGateBlockReason::LegacyFsdSettle:
        return "legacy_fsd_settle";
    }
    return "none";
}

static inline const char *fsdHealthStateName(FsdHealthState state)
{
    switch (state)
    {
    case FsdHealthState::Healthy:
        return "healthy";
    case FsdHealthState::NoSourceFrame:
        return "no_source_frame";
    case FsdHealthState::NotTriggered:
        return "not_triggered";
    case FsdHealthState::GateBlocked:
        return "gate_blocked";
    case FsdHealthState::TxDegraded:
        return "tx_degraded";
    case FsdHealthState::TxOkVehicleUnconfirmed:
        return "tx_ok_vehicle_unconfirmed";
    case FsdHealthState::Unknown:
        return "unknown";
    }
    return "unknown";
}

static inline const char *fsdDiagBusName(FsdDiagBus bus)
{
    switch (bus)
    {
    case FsdDiagBus::CanA:
        return "can_a";
    case FsdDiagBus::CanB:
        return "can_b";
    case FsdDiagBus::Unknown:
        return "unknown";
    }
    return "unknown";
}

static inline const char *fsdDiagDriverName(FsdDiagDriver driver)
{
    switch (driver)
    {
    case FsdDiagDriver::Twai:
        return "twai";
    case FsdDiagDriver::Mcp2515:
        return "mcp2515";
    case FsdDiagDriver::Unknown:
        return "unknown";
    }
    return "unknown";
}

struct FsdMuxDiag
{
    uint32_t rx = 0;
    uint32_t tx = 0;
    uint32_t err = 0;
    uint32_t lastRxMs = 0;
    uint32_t lastTxMs = 0;
    uint32_t lastTxSeq = 0;
    FsdSkipReason lastSkip = FsdSkipReason::None;
    FsdDiagBus bus = FsdDiagBus::Unknown;
    FsdDiagDriver driver = FsdDiagDriver::Unknown;
    uint8_t before[8] = {};
    uint8_t after[8] = {};

    void recordRx(uint32_t nowMs)
    {
        rx++;
        lastRxMs = nowMs;
    }

    void recordPath(FsdDiagBus frameBus, FsdDiagDriver frameDriver)
    {
        bus = frameBus;
        driver = frameDriver;
    }

    void recordBefore(const uint8_t data[8])
    {
        memcpy(before, data, 8);
    }

    void recordAfter(const uint8_t data[8])
    {
        memcpy(after, data, 8);
    }

    void recordTx(bool ok, uint32_t nowMs)
    {
        if (ok)
        {
            tx++;
            lastSkip = FsdSkipReason::Sent;
        }
        else
        {
            err++;
            lastSkip = FsdSkipReason::TxFail;
        }
        lastTxMs = nowMs;
    }
};

struct LegacyFsdDiag
{
    LegacyFsdPolicy policy = LegacyFsdPolicy::Stable;
    bool mux1Enable = false;
    bool profileWriteEnable = false;
    bool visionLimitClearEnable = false;
    bool triggered = false;
    bool forceRuntime = false;
    FsdTriggerSource triggerSource = FsdTriggerSource::Unknown;
    FsdHealthState health = FsdHealthState::Unknown;
    FsdGateBlockReason lastBlockedBy = FsdGateBlockReason::None;
    uint32_t firstMux0RxMs = 0;
    uint32_t firstMux1RxMs = 0;
    uint32_t firstTxOkMs = 0;
    uint32_t firstTxFailMs = 0;
    uint32_t txSeq = 0;
    FsdMuxDiag mux0;
    FsdMuxDiag mux1;
    FsdMuxDiag aux760;
    FsdMuxDiag aux1080;

    void markMux0Rx(uint32_t nowMs)
    {
        if (firstMux0RxMs == 0)
            firstMux0RxMs = nowMs;
    }

    void markMux1Rx(uint32_t nowMs)
    {
        if (firstMux1RxMs == 0)
            firstMux1RxMs = nowMs;
    }

    void recordMuxTx(FsdMuxDiag &mux, bool ok, uint32_t nowMs)
    {
        txSeq++;
        mux.recordTx(ok, nowMs);
        mux.lastTxSeq = txSeq;
    }

    void markTxResult(bool ok, uint32_t nowMs)
    {
        if (ok)
        {
            if (firstTxOkMs == 0)
                firstTxOkMs = nowMs;
            health = FsdHealthState::Healthy;
            lastBlockedBy = FsdGateBlockReason::None;
        }
        else
        {
            if (firstTxFailMs == 0)
                firstTxFailMs = nowMs;
            health = FsdHealthState::TxDegraded;
        }
    }
};

static inline uint32_t dashDiagNowMs()
{
#ifndef NATIVE_BUILD
    return millis();
#else
    static uint32_t nativeNowMs = 0;
    return ++nativeNowMs;
#endif
}

static constexpr uint32_t kLegacyFsdSourceStaleMs = 5000;

static inline FsdHealthState classifyLegacyFsdHealth(const LegacyFsdDiag &diag, uint32_t nowMs, bool enabled)
{
    if (!enabled)
        return FsdHealthState::Unknown;
    if (diag.mux0.lastRxMs == 0 || nowMs - diag.mux0.lastRxMs > kLegacyFsdSourceStaleMs)
        return FsdHealthState::NoSourceFrame;
    uint32_t latestFailTxMs = 0;
    uint32_t latestSentTxMs = 0;
    uint32_t latestFailTxSeq = 0;
    uint32_t latestSentTxSeq = 0;

    auto considerTx = [&](const FsdMuxDiag &mux)
    {
        if (mux.lastSkip == FsdSkipReason::TxFail)
        {
            if (mux.lastTxSeq > latestFailTxSeq)
                latestFailTxSeq = mux.lastTxSeq;
            if (mux.lastTxMs > latestFailTxMs)
                latestFailTxMs = mux.lastTxMs;
        }
        else if (mux.lastSkip == FsdSkipReason::Sent)
        {
            if (mux.lastTxSeq > latestSentTxSeq)
                latestSentTxSeq = mux.lastTxSeq;
            if (mux.lastTxMs > latestSentTxMs)
                latestSentTxMs = mux.lastTxMs;
        }
    };

    considerTx(diag.mux0);
    considerTx(diag.mux1);
    considerTx(diag.aux760);
    considerTx(diag.aux1080);

    if (latestFailTxSeq > 0 || latestSentTxSeq > 0)
    {
        if (latestFailTxSeq > latestSentTxSeq)
            return FsdHealthState::TxDegraded;
    }
    else if (latestFailTxMs > 0 && latestFailTxMs > latestSentTxMs)
        return FsdHealthState::TxDegraded;
    if (diag.health == FsdHealthState::GateBlocked)
        return FsdHealthState::GateBlocked;
    if (diag.health == FsdHealthState::NotTriggered)
        return FsdHealthState::NotTriggered;
    if (diag.mux0.tx > 0)
        return FsdHealthState::Healthy;
    return diag.health;
}
