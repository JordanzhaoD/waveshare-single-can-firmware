#pragma once

// ─── CAN driver selection ──────────────────────────────────
// Exactly one driver define should be enabled. CI rewrites this
// file through scripts/platformio_set_profile.py before builds.
#define DRIVER_T2CAN_DUAL
// #define DRIVER_MCP2515
// #define DRIVER_SAME51
// #define DRIVER_TWAI
// #define DRIVER_ESP32_EXT_MCP2515

// ─── Vehicle generation ────────────────────────────────────
// LILYGO T-2CAN release builds default to HW4; change locally if needed.
// #define LEGACY
// #define HW3
#define HW4

// ─── Optional features ────────────────────────────────────
#define ISA_SPEED_CHIME_SUPPRESS
#define EMERGENCY_VEHICLE_DETECTION
// #define BYPASS_TLSSC_REQUIREMENT
// #define NAG_KILLER
#define ENHANCED_AUTOPILOT
// #define INJECTION_AFTER_AP

// ─── Dashboard credentials ─────────────────────────────────
// PLACEHOLDERS — replace with your own values before building for real use.
#define DASH_SSID      "CHANGE_ME_SSID"
#define DASH_PASS      "CHANGE_ME_PASS"
#define DASH_OTA_USER  "admin"
#define DASH_OTA_PASS  "CHANGE_ME_OTA_PASS"
