#pragma once

#include <cstdint>
#include <cstring>

struct can_frame
{
    uint32_t can_id = 0;
    uint8_t can_dlc = 0;
    uint8_t data[8] = {};
};

class MCP2515
{
public:
    enum ERROR
    {
        ERROR_OK = 0,
        ERROR_FAIL = 1,
        ERROR_ALLTXBUSY = 2,
        ERROR_FAILINIT = 3,
        ERROR_FAILTX = 4,
        ERROR_NOMSG = 5
    };

    enum MASK
    {
        MASK0,
        MASK1
    };

    enum RXF
    {
        RXF0 = 0,
        RXF1 = 1,
        RXF2 = 2,
        RXF3 = 3,
        RXF4 = 4,
        RXF5 = 5
    };

    enum EFLG : uint8_t
    {
        EFLG_TXBO = (1 << 5)
    };

    explicit MCP2515(uint8_t) {}

    void reset() { resetCount++; }

    ERROR setBitrate(int speed, int clock)
    {
        lastSpeed = speed;
        lastClock = clock;
        bitrateCount++;
        return bitrateResult;
    }

    void setNormalMode() { normalModeCount++; }
    void setConfigMode() {}
    void setFilterMask(MASK, bool, uint32_t) {}
    void setFilter(RXF, bool, uint32_t) {}

    ERROR readMessage(can_frame *frame)
    {
        if (readResult != ERROR_OK)
            return readResult;
        *frame = nextReadFrame;
        return ERROR_OK;
    }

    ERROR sendMessage(const can_frame *frame)
    {
        lastSentFrame = *frame;
        return sendResult;
    }

    uint8_t getErrorFlags()
    {
        errorFlagsReadCount++;
        return errorFlags;
    }

    static void resetFake()
    {
        resetCount = 0;
        bitrateCount = 0;
        normalModeCount = 0;
        errorFlagsReadCount = 0;
        lastSpeed = 0;
        lastClock = 0;
        bitrateResult = ERROR_OK;
        readResult = ERROR_NOMSG;
        sendResult = ERROR_OK;
        errorFlags = 0;
        nextReadFrame = {};
        lastSentFrame = {};
    }

    static int resetCount;
    static int bitrateCount;
    static int normalModeCount;
    static int errorFlagsReadCount;
    static int lastSpeed;
    static int lastClock;
    static ERROR bitrateResult;
    static ERROR readResult;
    static ERROR sendResult;
    static uint8_t errorFlags;
    static can_frame nextReadFrame;
    static can_frame lastSentFrame;
};
