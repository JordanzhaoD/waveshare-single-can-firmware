#pragma once

#include <vector>
#include "../can_frame_types.h"
#include "can_driver.h"

class MockDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    std::vector<CanFrame> sent;
    bool sendOk = true;

    bool init() override { return true; }
    void setFilters(const uint32_t * /*ids*/, uint8_t /*count*/) override {}
    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame & /*frame*/) override
    {
        return false;
    }

    bool send(const CanFrame &frame) override
    {
        sent.push_back(frame);
        if (onSendFrame)
            onSendFrame(frame, sendOk);
        return sendOk;
    }

    void reset()
    {
        sent.clear();
        sendOk = true;
    }
};
