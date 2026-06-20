#pragma once

#include <cstdint>

#define INPUT_PULLUP 0x2
#define FALLING 0x2
#define CAN_500KBPS 500
#define MCP_16MHZ 16

extern uint32_t fakeMillis;
extern uint32_t fakeDelayMs;

inline uint32_t millis()
{
    return fakeMillis;
}

inline void delay(uint32_t ms)
{
    fakeDelayMs += ms;
    fakeMillis += ms;
}

inline void pinMode(uint8_t, uint8_t) {}
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
