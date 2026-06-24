#pragma once

#include "can_frame_types.h"
#include "shared_types.h"

#if defined(ISA_SPEED_CHIME_SUPPRESS) && !defined(ESP32_DASHBOARD)
inline constexpr bool kIsaSpeedChimeSuppressDefaultEnabled = true;
inline constexpr bool kIsaSpeedChimeSuppressBuildEnabled = true;
#else
inline constexpr bool kIsaSpeedChimeSuppressDefaultEnabled = false;
inline constexpr bool kIsaSpeedChimeSuppressBuildEnabled = false;
#endif

#if defined(EMERGENCY_VEHICLE_DETECTION) && !defined(ESP32_DASHBOARD)
inline constexpr bool kEmergencyVehicleDetectionDefaultEnabled = true;
inline constexpr bool kEmergencyVehicleDetectionBuildEnabled = true;
#else
inline constexpr bool kEmergencyVehicleDetectionDefaultEnabled = false;
inline constexpr bool kEmergencyVehicleDetectionBuildEnabled = false;
#endif

#if defined(ENHANCED_AUTOPILOT) && !defined(ESP32_DASHBOARD)
inline constexpr bool kEnhancedAutopilotDefaultEnabled = true;
inline constexpr bool kEnhancedAutopilotBuildEnabled = true;
#else
inline constexpr bool kEnhancedAutopilotDefaultEnabled = false;
inline constexpr bool kEnhancedAutopilotBuildEnabled = false;
#endif

#if defined(NAG_KILLER) && !defined(ESP32_DASHBOARD)
inline constexpr bool kNagKillerDefaultEnabled = true;
inline constexpr bool kNagKillerBuildEnabled = true;
#else
inline constexpr bool kNagKillerDefaultEnabled = false;
inline constexpr bool kNagKillerBuildEnabled = false;
#endif

#if defined(INJECTION_AFTER_AP) || defined(DASH_INJECTION_AFTER_AP)
inline constexpr bool kInjectionAfterApBuildEnabled = true;
#else
inline constexpr bool kInjectionAfterApBuildEnabled = false;
#endif

inline Shared<bool> forceActivateRuntime{false};
inline Shared<bool> isaSpeedChimeSuppressRuntime{kIsaSpeedChimeSuppressDefaultEnabled};
inline Shared<bool> emergencyVehicleDetectionRuntime{kEmergencyVehicleDetectionDefaultEnabled};
inline Shared<bool> enhancedAutopilotRuntime{kEnhancedAutopilotDefaultEnabled};
inline Shared<bool> nagKillerRuntime{kNagKillerDefaultEnabled};
// Opt-in torque-tamper mode for NagHandler 0x370 echo (1.80 Nm fixed torque).
// DEFAULT false = PASSTHROUGH (torque bytes untouched). True = TORQUE_TAMPER.
// Torque-tamper is the documented primary-suspect vector of the 2026-06-19 EPAS
// fault (docs/EPAS-NAG-REMOVAL-INCIDENT.md) — opt-in only, never the default.
inline Shared<bool> nagTorqueTamperRuntime{false};

// Pure Soft Engage angle-gate decision (native-testable; no state).
// The Legacy dashboard gate dashLegacyFsdActivationAllowed() calls this once
// AP-settle is being evaluated. Returns true iff the Legacy 0x3EE bit46
// activation may fire NOW; false means hold bit46 off until the wheel nears
// centre (|steerAngleX10| <= angleThreshX10 AND steerValidity==0) or the
// timeout elapses. Mirrors upstream flipper-tesla-fsd v2.16-beta.10 Soft Engage.
// enabled=false or alreadySent=true or settled=false → true (not our job).
inline bool dashSoftEngageRelease(bool enabled, bool alreadySent,
                                  bool steerSeen, uint8_t steerValidity,
                                  int16_t steerAngleX10,
                                  bool settled, bool timeout,
                                  int angleThreshX10)
{
    if (!enabled)    return true;   // toggle OFF → V1.0.3 behaviour
    if (alreadySent) return true;   // latched this episode → ignore angle
    if (!settled)    return true;   // settle gate hasn't passed (not our job)
    const bool centred = steerSeen
                         && steerValidity == 0
                         && abs(static_cast<int>(steerAngleX10)) <= angleThreshX10;
    return centred || timeout;
}

inline bool enhancedAutopilotInjectionAllowed(bool adEnabled)
{
    return !kInjectionAfterApBuildEnabled || adEnabled;
}

inline uint8_t readMuxID(const CanFrame &frame)
{
    return frame.data[0] & 0x07;
}

inline bool isFSDSelectedInUI(const CanFrame &frame)
{
    return (frame.data[4] >> 6) & 0x01;
}

inline uint8_t readGTWAutopilot(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[5] >> 2) & 0x07);
}

inline uint8_t readDASAutopilotStatus(const CanFrame &frame)
{
    return frame.data[0] & 0x0F;
}

inline bool isDASAutopilotActive(uint8_t status)
{
    // DAS_autopilotState (0x399 byte0 low nibble). AP is actively engaged on
    // states 3-5 (older firmware) and 6 (newer firmware incl. CN 2026.8.3.6).
    // State 8 (handover/warning) and 9 (fault) are NOT active.
    return status >= 3 && status <= 6;
}

inline uint8_t readVehicleGear(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[7] >> 3) & 0x07);
}

// DI_systemStatus (CAN ID 280 / 0x118) DI_gear: byte 2 bits 5-7
// Values: 0=INVALID, 1=P, 2=R, 3=N, 4=D, 7=SNA
inline uint8_t readDIGear(const CanFrame &frame)
{
    return static_cast<uint8_t>((frame.data[2] >> 5) & 0x07);
}

inline bool isVehicleParked(uint8_t gear)
{
    // Live gear must fail closed for INVALID (0), SNA (7), and reserved values.
    // Only true Park (1) opens the parked branch of AP Injection Gate.
    // Previous behavior treated 0/7 as parked (for DI-asleep/Sentry cold
    // approach); upstream beta3 safety parity requires fail-closed: a missing
    // or SNA gear reading must NOT open the injection gate.
    return gear == 1;
}

inline const char *describeGTWAutopilot(uint8_t value)
{
    switch (value)
    {
    case 0:
        return "NONE";
    case 1:
        return "HIGHWAY";
    case 2:
        return "ENHANCED";
    case 3:
        return "SELF_DRIVING";
    case 4:
        return "BASIC";
    default:
        return "UNKNOWN";
    }
}

inline void setSpeedProfileV12V13(CanFrame &frame, int profile)
{
    if (profile > 2)
        profile = 2; // Clamp: profiles 3/4 are HW4-only
    frame.data[6] &= ~0x06;
    frame.data[6] |= (profile << 1);
}

inline void setSpeedProfileHW4(CanFrame &frame, int profile)
{
    frame.data[7] &= static_cast<uint8_t>(~0x70);
    frame.data[7] |= static_cast<uint8_t>((profile & 0x07) << 4);
}

inline uint8_t computeVehicleChecksum(const CanFrame &frame, uint8_t checksumByteIndex = 7)
{
    if (checksumByteIndex >= frame.dlc)
        return 0;

    uint16_t sum = static_cast<uint16_t>(frame.id & 0xFF) +
                   static_cast<uint16_t>((frame.id >> 8) & 0xFF);
    for (uint8_t i = 0; i < frame.dlc; ++i)
    {
        if (i == checksumByteIndex)
            continue;
        sum += frame.data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

inline void setBit(CanFrame &frame, int bit, bool value)
{
    if (bit < 0 || bit >= 64)
        return; // bounds guard: CanFrame.data is 8 bytes
    int byteIndex = bit / 8;
    int bitIndex = bit % 8;
    uint8_t mask = static_cast<uint8_t>(1U << bitIndex);
    if (value)
    {
        frame.data[byteIndex] |= mask;
    }
    else
    {
        frame.data[byteIndex] &= static_cast<uint8_t>(~mask);
    }
}
