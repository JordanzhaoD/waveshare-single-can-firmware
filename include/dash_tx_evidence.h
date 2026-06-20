#pragma once

#include <cstdint>
#include <cstring>

enum class DashTxError : uint8_t
{
    None = 0,
    CanNotReady = 1,
    TxFail = 2,
    NativeMissing = 3,
};

static inline const char *dashTxErrorName(DashTxError error)
{
    switch (error)
    {
    case DashTxError::None:
        return "none";
    case DashTxError::CanNotReady:
        return "can_b_not_ready";
    case DashTxError::TxFail:
        return "tx_fail";
    case DashTxError::NativeMissing:
        return "native_missing";
    }
    return "none";
}

struct DashTxEvidence
{
    uint32_t id = 0;
    uint8_t bus = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    uint32_t txOk = 0;
    uint32_t txErr = 0;
    uint32_t lastTxMs = 0;
    DashTxError lastError = DashTxError::None;

    void recordAttempt(const uint32_t frameId, const uint8_t frameBus, const uint8_t frameDlc, const uint8_t frameData[8], uint32_t nowMs)
    {
        id = frameId;
        bus = frameBus;
        dlc = frameDlc <= 8 ? frameDlc : 8;
        memset(data, 0, sizeof(data));
        memcpy(data, frameData, dlc);
        lastTxMs = nowMs;
    }

    void recordResult(bool ok)
    {
        if (ok)
        {
            txOk++;
            lastError = DashTxError::None;
        }
        else
        {
            txErr++;
            lastError = DashTxError::TxFail;
        }
    }

    void recordError(DashTxError error, uint32_t nowMs)
    {
        txErr++;
        lastError = error;
        lastTxMs = nowMs;
    }
};
