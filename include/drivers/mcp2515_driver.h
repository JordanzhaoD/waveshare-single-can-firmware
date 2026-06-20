#pragma once

#include <SPI.h>
#include <mcp2515.h>
#include <memory>
#include "../can_frame_types.h"
#include "can_driver.h"

class MCP2515Driver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = true;

    explicit MCP2515Driver(uint8_t csPin) : mcp_(csPin) {}

    bool init() override
    {
        mcp_.reset();
        MCP2515::ERROR e = mcp_.setBitrate(CAN_500KBPS, MCP_16MHZ);
        if (e != MCP2515::ERROR_OK)
            return false;
        mcp_.setNormalMode();
        consecutiveTxFailures_ = 0;
        return true;
    }

    void setFilters(const uint32_t *ids, uint8_t count) override
    {
        filterCount_ = count > kMaxFilters ? kMaxFilters : count;
        for (uint8_t i = 0; i < filterCount_; i++)
            filterIds_[i] = ids[i];
        applyFilters();
    }

    bool enableInterrupt(void (*onReady)()) override
    {
        pinMode(PIN_CAN_INTERRUPT, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_CAN_INTERRUPT), onReady, FALLING);
        return true;
    }

    bool read(CanFrame &frame) override
    {
        checkBusOff();

        can_frame raw;
        if (mcp_.readMessage(&raw) != MCP2515::ERROR_OK)
            return false;
        frame.id = raw.can_id;
        frame.dlc = raw.can_dlc;
        memcpy(frame.data, raw.data, 8);
        return true;
    }

    bool send(const CanFrame &frame) override
    {
        can_frame raw;
        raw.can_id = frame.id;
        raw.can_dlc = frame.dlc;
        memcpy(raw.data, frame.data, 8);
        bool ok = mcp_.sendMessage(&raw) == MCP2515::ERROR_OK;
        if (ok)
        {
            consecutiveTxFailures_ = 0;
        }
        else
        {
            consecutiveTxFailures_++;
            if (consecutiveTxFailures_ >= kMaxConsecutiveTxFailures)
                tryRecover();
        }
        if (onSendFrame)
            onSendFrame(frame, ok);
        return ok;
    }

private:
    static constexpr uint8_t kMaxFilters = 6;
    static constexpr uint8_t kMaxConsecutiveTxFailures = 5;
    static constexpr uint32_t kErrorCheckIntervalMs = 1000;
    static constexpr uint32_t kRecoveryCooldownMs = 3000;

    void applyFilters()
    {
        if (filterCount_ == 0)
            return;

        mcp_.setConfigMode();
        mcp_.setFilterMask(MCP2515::MASK0, false, 0x7FF);
        mcp_.setFilter(MCP2515::RXF0, false, filterIds_[0]);
        mcp_.setFilter(MCP2515::RXF1, false, filterCount_ > 1 ? filterIds_[1] : filterIds_[0]);
        mcp_.setFilterMask(MCP2515::MASK1, false, 0x7FF);
        mcp_.setFilter(MCP2515::RXF2, false, filterCount_ > 2 ? filterIds_[2] : filterIds_[0]);
        mcp_.setFilter(MCP2515::RXF3, false, filterCount_ > 3 ? filterIds_[3] : filterIds_[0]);
        mcp_.setFilter(MCP2515::RXF4, false, filterCount_ > 4 ? filterIds_[4] : filterIds_[0]);
        mcp_.setFilter(MCP2515::RXF5, false, filterCount_ > 5 ? filterIds_[5] : filterIds_[0]);
        mcp_.setNormalMode();
    }

    void checkBusOff()
    {
        uint32_t now = millis();
        if (now - lastErrorCheck_ < kErrorCheckIntervalMs)
            return;
        lastErrorCheck_ = now;

        if (mcp_.getErrorFlags() & MCP2515::EFLG_TXBO)
            tryRecover();
    }

    void tryRecover()
    {
        uint32_t now = millis();
        if (now - lastRecovery_ < kRecoveryCooldownMs)
            return;
        lastRecovery_ = now;

        mcp_.reset();
        delay(10);
        if (mcp_.setBitrate(CAN_500KBPS, MCP_16MHZ) != MCP2515::ERROR_OK)
            return;
        consecutiveTxFailures_ = 0;
        if (filterCount_ > 0)
            applyFilters();
        else
            mcp_.setNormalMode();
    }

    MCP2515 mcp_;
    uint32_t filterIds_[kMaxFilters] = {};
    uint8_t filterCount_ = 0;
    uint8_t consecutiveTxFailures_ = 0;
    uint32_t lastErrorCheck_ = 0;
    uint32_t lastRecovery_ = 0 - kRecoveryCooldownMs;
};
